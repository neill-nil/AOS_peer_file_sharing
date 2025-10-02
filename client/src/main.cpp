#include "hash_utils.hpp"
#include "download_manager.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <cerrno>
#include <sstream>

using namespace std;

vector<pair<string,int>> load_trackers(const string& filename) {
    vector<pair<string,int>> trackers;
    int fd = open(filename.c_str(), O_RDONLY);
    if (fd < 0) { perror("open tracker_info_file"); exit(1); }

    string line; char c;
    while (read(fd,&c,1)==1) {
        if (c=='\n') {
            if (!line.empty()) {
                size_t colon=line.find(':');
                if (colon!=string::npos) {
                    trackers.push_back({line.substr(0,colon), stoi(line.substr(colon+1))});
                }
            }
            line.clear();
        } else line.push_back(c);
    }
    if (!line.empty()) {
        size_t colon=line.find(':');
        if (colon!=string::npos) {
            trackers.push_back({line.substr(0,colon), stoi(line.substr(colon+1))});
        }
    }
    close(fd);
    return trackers;
}

int connect_to_tracker(const string& client_ip, int client_port,
                       const string& tracker_ip, int tracker_port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(client_port);
    inet_pton(AF_INET, client_ip.c_str(), &local.sin_addr);
    if (bind(sock, (struct sockaddr*)&local, sizeof(local)) < 0) {
        close(sock);
        return -1;
    }

    struct sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(tracker_port);
    inet_pton(AF_INET, tracker_ip.c_str(), &serv_addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        close(sock);
        return -1;
    }

    return sock;
}

// helper: read available data from socket with small timeout to avoid blocking forever
static string read_available_with_timeout(int sock, int max_iterations = 80, int sleep_us = 25000) {
    string resp;
    char buf[2048];

    // save flags and set non-blocking
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags >= 0) fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    for (int i = 0; i < max_iterations; ++i) {
        ssize_t n = read(sock, buf, sizeof(buf));
        if (n > 0) {
            resp.append(buf, n);
            // keep reading in case the tracker sent multi-line response
            // small optimization: if we've seen two newlines in a row, stop early
            if (resp.find("\n\n") != string::npos) break;
            // otherwise continue looping to gather more data
            usleep(2000);
            continue;
        } else if (n == 0) {
            // peer closed socket
            break;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // no data right now; wait a bit and retry
                usleep(sleep_us);
                continue;
            } else {
                // real error
                break;
            }
        }
    }

    // restore flags
    if (flags >= 0) fcntl(sock, F_SETFL, flags);

    return resp;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "Usage: " << argv[0] << " <CLIENT_IP:CLIENT_PORT> <tracker_info_file>\n";
        return 1;
    }

    string client_ip_port = argv[1];
    string tracker_info_file = argv[2];

    size_t colon = client_ip_port.find(':');
    if (colon == string::npos) {
        cerr << "Invalid CLIENT_IP:CLIENT_PORT format\n";
        return 1;
    }
    string client_ip = client_ip_port.substr(0, colon);
    int client_port = stoi(client_ip_port.substr(colon+1));

    auto trackers = load_trackers(tracker_info_file);
    if (trackers.empty()) {
        cerr << "No trackers found in " << tracker_info_file << endl;
        return 1;
    }

    // start peer server for piece uploads
    start_peer_server(client_ip, client_port);

    int active = -1;
    int tracker_sock = -1;

    // Initial connection
    for (int i = 0; i < (int)trackers.size(); ++i) {
        tracker_sock = connect_to_tracker(client_ip, client_port,
                                          trackers[i].first, trackers[i].second);
        if (tracker_sock >= 0) {
            active = i;
            cout << "✅ Connected to tracker at "
                 << trackers[i].first << ":" << trackers[i].second << endl;
            break;
        }
    }

    if (active == -1) {
        cerr << "❌ Could not connect to any tracker at startup\n";
        return 1;
    }

    // Command loop
    while (true) {
        cout << "> ";
        string command;
        if (!getline(cin, command)) break;
        if (command.empty()) continue;

        if (command.rfind("upload_file", 0) == 0) {
            string expanded = preprocess_upload_file(command);
            if (expanded.empty()) continue;
            command = expanded;
        }
        else if (command.rfind("login", 0) == 0) {
            istringstream iss(command);
            string cmd, user, pass;
            iss >> cmd >> user >> pass;
            command = "login " + user + " " + pass + " " + client_ip + ":" + to_string(client_port);
        }
        else if (command.rfind("download_file", 0) == 0) {
            istringstream iss(command);
            string cmd, group_id, file_name, dest_path;
            iss >> cmd >> group_id >> file_name >> dest_path;

            string tracker_req = "get_file_info " + group_id + " " + file_name + "\n";
            if (send(tracker_sock, tracker_req.c_str(), tracker_req.size(), 0) < 0) {
                cout << "⚠️ Tracker connection broken while requesting metadata\n";
                break;
            }

            // read available response with timeout (non-blocking spinner)
            string response = read_available_with_timeout(tracker_sock);

            // DEBUG: print raw tracker response for get_file_info
            cout << "[DEBUG get_file_info raw]:\n" << response << endl;

            if (response.empty()) {
                cout << "ERROR: No response from tracker for get_file_info\n";
                continue;
            }

            // Trim leading whitespace for checks
            auto ltrim = [](string &s) {
                size_t a = 0;
                while (a < s.size() && isspace((unsigned char)s[a])) ++a;
                if (a) s.erase(0, a);
            };
            ltrim(response);

            if (response.rfind("ERROR", 0) == 0) {
                cout << response;
            } else if (response.rfind("FILE_INFO", 0) == 0 || response.rfind("OK", 0) == 0) {
                // some trackers may send FILE_INFO header; adjust to parse both
                // We will parse lines robustly
                istringstream resp(response);
                string first;
                resp >> first;

                // If tracker used "OK" format, parse accordingly.
                // We'll attempt to detect both formats.
                long long file_size = 0;
                string file_hash;
                int piece_count = 0;
                vector<string> piece_hashes;
                vector<string> peers;

                if (first == "FILE_INFO") {
                    // FILE_INFO <group> <file>
                    string group, fname;
                    resp >> group >> fname;
                    string line;
                    while (getline(resp, line)) {
                        if (line.find("size:") == 0) {
                            file_size = stoll(line.substr(5));
                        } else if (line.find("hash:") == 0) {
                            file_hash = line.substr(5);
                        } else if (line.find("pieces:") == 0) {
                            piece_count = stoi(line.substr(7));
                        } else if (line.find("peers:") == 0) {
                            continue;
                        } else if (!line.empty() && isspace((unsigned char)line[0])) {
                            string value = line;
                            value.erase(0, value.find_first_not_of(" \t"));
                            if ((int)piece_hashes.size() < piece_count) piece_hashes.push_back(value);
                            else peers.push_back(value);
                        }
                    }
                } else {
                    // Assume OK <size> <file_hash> <piece_count> <piece_hashes...> peers: <peer list...>
                    // We'll parse tokens
                    // Reset stream
                    resp.clear();
                    resp.seekg(0);
                    string token;
                    resp >> token; // OK
                    resp >> file_size >> file_hash >> piece_count;
                    for (int i = 0; i < piece_count; ++i) {
                        string ph;
                        resp >> ph;
                        piece_hashes.push_back(ph);
                    }
                    // next token should be "peers:" (maybe)
                    string tag;
                    resp >> tag;
                    if (tag == "peers:" || tag == "peers") {
                        string peer;
                        while (resp >> peer) peers.push_back(peer);
                    } else {
                        // no peers tag: try to read remaining tokens as peers
                        if (!tag.empty()) peers.push_back(tag);
                        string peer;
                        while (resp >> peer) peers.push_back(peer);
                    }
                }

                if (file_size > 0 && !piece_hashes.empty() && !peers.empty()) {
                    cout << "📥 Starting download of " << file_name
                        << " into " << dest_path
                        << " from " << peers.size() << " peers...\n";

                    start_download(group_id, file_name, dest_path,
                                file_size, piece_hashes, file_hash,
                                peers, 4);
                } else {
                    cout << "ERROR: Incomplete metadata from tracker\n";
                }
            } else {
                cout << "ERROR: Unexpected response from tracker\n";
            }
            continue;
        }
        else if (command == "show_downloads") {
            show_downloads();
            continue;
        }

        // Send other commands
        command.push_back('\n');  // ensure newline terminated
        if (send(tracker_sock, command.c_str(), command.size(), 0) < 0) {
            cout << "⚠️ Tracker connection lost\n";
            break;
        }

        // read server reply (simple read with small timeout)
        string reply = read_available_with_timeout(tracker_sock);
        if (!reply.empty()) {
            cout << reply;
        } else {
            cout << "(no response)\n";
        }
    }

    close(tracker_sock);
    return 0;
}

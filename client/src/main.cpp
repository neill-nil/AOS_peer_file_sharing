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

// Try connecting to a tracker using specific client bind
int try_connect_and_close(const string& client_ip, int client_port,
                          const string& tracker_ip, int tracker_port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(client_port);
    if (inet_pton(AF_INET, client_ip.c_str(), &local.sin_addr) <= 0) {
        close(sock);
        return -1;
    }
    if (bind(sock, (struct sockaddr*)&local, sizeof(local)) < 0) {
        close(sock);
        return -1;
    }

    struct sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(tracker_port);
    if (inet_pton(AF_INET, tracker_ip.c_str(), &serv_addr.sin_addr) <= 0) {
        close(sock);
        return -1;
    }

    int res = connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
    close(sock);
    return res == 0 ? 0 : -1;
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
    for (int i = 0; i < (int)trackers.size(); ++i) {
        if (try_connect_and_close(client_ip, client_port, trackers[i].first, trackers[i].second) == 0) {
            active = i;
            break;
        }
    }

    if (active == -1) {
        cout << "⚠️ No tracker reachable at startup; will try them on each command\n";
        active = 0;
    } else {
        cout << "✅ Connected to tracker at " << trackers[active].first << ":" << trackers[active].second << endl;
    }

    while (true) {
        cout << "> ";
        string command;
        if (!getline(cin, command)) break;
        if (command.empty()) continue;

        if (command.rfind("upload_file", 0) == 0) {
            string expanded = preprocess_upload_file(command);
            if (expanded.empty()) continue;
            command = expanded; // send expanded cmd to tracker
        }
        else if (command.rfind("download_file", 0) == 0) {
            istringstream iss(command);
            string cmd, group_id, file_name, dest_path;
            iss >> cmd >> group_id >> file_name >> dest_path;
            // TODO: retrieve metadata from tracker (file_size, hashes, peers)
            // For now, stub with dummy values:
            vector<string> dummy_hashes; string dummy_filehash;
            start_download(group_id, file_name, dest_path, 0, dummy_hashes, dummy_filehash, {}, 2);
            continue; // handled locally
        }
        else if (command == "show_downloads") {
            show_downloads();
            continue;
        }

        bool sent = false;
        int attempts = 0;
        while (attempts < (int)trackers.size()) {
            auto [ip, port] = trackers[active];

            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0) { perror("socket"); return 1; }

            int opt = 1;
            setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

            struct sockaddr_in local{};
            local.sin_family = AF_INET;
            local.sin_port = htons(client_port);
            inet_pton(AF_INET, client_ip.c_str(), &local.sin_addr);
            bind(sock, (struct sockaddr*)&local, sizeof(local));

            struct sockaddr_in serv_addr{};
            serv_addr.sin_family = AF_INET;
            serv_addr.sin_port = htons(port);
            inet_pton(AF_INET, ip.c_str(), &serv_addr.sin_addr);

            if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
                close(sock);
                active = (active + 1) % trackers.size();
                attempts++;
                continue;
            }

            send(sock, command.c_str(), command.size(), 0);

            char buffer[1024] = {0};
            int valread = read(sock, buffer, sizeof(buffer) - 1);
            if (valread > 0) {
                buffer[valread] = '\0';
                cout << buffer;
            } else {
                cout << "(no response)\n";
            }

            close(sock);
            sent = true;
            break;
        }

        if (!sent) {
            cerr << "❌ All trackers unreachable\n";
        }
    }

    return 0;
}

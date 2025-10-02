#include "download_manager.hpp"
#include "hash_utils.hpp"   // provides sha1_hex(const char*, size_t) and helpers

#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <algorithm>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <cstring>
#include <sstream>
#include <vector>

using namespace std;

map<string, DownloadState> downloads;
mutex downloads_mutex;

static const size_t PIECE_SIZE = 512 * 1024; // 512KB
static const int MAX_RETRIES_PER_PIECE = 4;

static string make_key(const string& group, const string& file) {
    return group + ":" + file;
}

// safer write at offset for concurrent threads
static bool write_piece_to_file(const string& path, int piece_idx, const vector<char>& data) {
    int fd = open(path.c_str(), O_WRONLY | O_CREAT, 0666);
    if (fd < 0) return false;
    off_t offset = (off_t)piece_idx * (off_t)PIECE_SIZE;
    ssize_t w = pwrite(fd, data.data(), data.size(), offset);
    close(fd);
    return (w == (ssize_t)data.size());
}

static bool parse_hostport(const string& hp, string& host, int& port) {
    size_t c = hp.find(':');
    if (c == string::npos) return false;
    host = hp.substr(0, c);
    try {
        port = stoi(hp.substr(c + 1));
    } catch (...) {
        return false;
    }
    return true;
}

// Fetch a piece from peer. Protocol: send "GET_PIECE <file_name> <piece_index>\n"
static bool fetch_piece_from_peer(const string& peer_hp,
                                  const string& file_name,
                                  int piece_index,
                                  vector<char>& out_buf) {
    string host; int port;
    if (!parse_hostport(peer_hp, host, port)) return false;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) { close(sock); return false; }

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) { 
        close(sock); 
        cerr << "⚠️ Failed to connect to peer " << peer_hp << endl;
        return false; 
    }

    string req = "GET_PIECE " + file_name + " " + to_string(piece_index) + "\n";
    if (send(sock, req.c_str(), req.size(), 0) != (ssize_t)req.size()) { 
        cerr << "⚠️ Failed to send request to " << peer_hp << endl;
        close(sock); 
        return false; 
    }

    out_buf.clear();
    out_buf.reserve(PIECE_SIZE);
    ssize_t n;
    char buf[8192];
    size_t total = 0;
    while ((n = read(sock, buf, sizeof(buf))) > 0) {
        out_buf.insert(out_buf.end(), buf, buf + n);
        total += n;
        if (total >= PIECE_SIZE) break;
    }
    close(sock);

    if (out_buf.empty()) {
        cerr << "⚠️ Peer " << peer_hp << " sent no data for piece " << piece_index << endl;
        return false;
    }
    return true;
}

static bool allocate_file_space(const string& path, long long size) {
    int fd = open(path.c_str(), O_RDWR | O_CREAT, 0666);
    if (fd < 0) return false;
#if defined(_POSIX_VERSION)
    int res = posix_fallocate(fd, 0, (off_t)size);
    if (res != 0) {
        if (size > 0) {
            if (lseek(fd, size - 1, SEEK_SET) < 0) { close(fd); return false; }
            char zero = 0;
            if (write(fd, &zero, 1) != 1) { close(fd); return false; }
        }
    }
#else
    if (size > 0) {
        if (lseek(fd, size - 1, SEEK_SET) < 0) { close(fd); return false; }
        char zero = 0;
        if (write(fd, &zero, 1) != 1) { close(fd); return false; }
    }
#endif
    close(fd);
    return true;
}

// worker that repeatedly grabs a pending piece and attempts download from peers
static void piece_worker(const string& key, const vector<string>& peers, int concurrency_limit) {
    while (true) {
        int piece_idx = -1;
        string file_name, dest_path;

        {   // lock only once here
            lock_guard<mutex> lg(downloads_mutex);
            auto it = downloads.find(key);
            if (it == downloads.end()) return;
            DownloadState &st = it->second;
            if (st.completed || st.cancelled) return;

            for (int i = 0; i < st.piece_count; ++i) {
                if (!st.done[i] && !st.in_progress_flags[i] && st.in_progress < concurrency_limit) {
                    st.in_progress_flags[i] = true;
                    st.in_progress++;
                    piece_idx = i;
                    file_name = st.file_name;
                    dest_path = st.dest_path;
                    break;
                }
            }

            if (piece_idx == -1) {
                bool all_done = all_of(st.done.begin(), st.done.end(), [](bool d){ return d; });
                if (all_done) { st.completed = true; return; }
            }
        }

        if (piece_idx == -1) {
            this_thread::sleep_for(chrono::milliseconds(100));
            continue;
        }

        bool success = false;
        int peer_count = (int)peers.size();
        for (int attempt = 0; attempt < MAX_RETRIES_PER_PIECE && !success; ++attempt) {
            int peer_idx = peer_count ? (attempt % peer_count) : -1;
            string peer = (peer_idx >= 0) ? peers[peer_idx] : string();
            vector<char> piece_data;
            if (!peer.empty() && fetch_piece_from_peer(peer, file_name, piece_idx, piece_data)) {
                string got_hash = sha1_hex(piece_data.data(), piece_data.size());
                string expected;
                {
                    lock_guard<mutex> lg(downloads_mutex);
                    expected = downloads[key].piece_hashes[piece_idx];
                }
                if (got_hash == expected && write_piece_to_file(dest_path, piece_idx, piece_data)) {
                    lock_guard<mutex> lg(downloads_mutex);
                    if (downloads.find(key) != downloads.end()) {
                        downloads[key].done[piece_idx] = true;
                        downloads[key].pieces_done++;
                        downloads[key].piece_owner_idx[piece_idx] =
                            (downloads[key].piece_owner_idx[piece_idx] + 1) % max(1, peer_count);
                    }
                    success = true;
                }
            }
        }

        {   // mark not in-progress
            lock_guard<mutex> lg(downloads_mutex);
            if (downloads.find(key) != downloads.end()) {
                downloads[key].in_progress_flags[piece_idx] = false;
                downloads[key].in_progress = max(0, downloads[key].in_progress - 1);
            }
        }
    }
}

void start_download(const string &group_id,
                    const string &file_name,
                    const string &dest_path,
                    long long file_size,
                    const vector<string> &piece_hashes,
                    const string &file_hash,
                    const vector<string> &peers,
                    int concurrency) 
{
    if (peers.empty()) {
        cout << "ERROR: No peers available for this file\n";
        return;
    }

    string key = make_key(group_id, file_name);
    {
        lock_guard<mutex> lg(downloads_mutex);
        if (downloads.count(key)) {
            cout << "⚠️ Download already in progress for " << file_name << "\n";
            return;
        }
        DownloadState st;
        st.group_id = group_id;
        st.file_name = file_name;
        st.dest_path = dest_path;
        st.file_size = file_size;
        st.piece_count = (int)piece_hashes.size();
        st.piece_hashes = piece_hashes;
        st.file_hash = file_hash;
        st.done.assign(st.piece_count, false);
        st.in_progress_flags.assign(st.piece_count, false);
        st.piece_owner_idx.assign(st.piece_count, 0);
        st.pieces_done = 0;
        st.in_progress = 0;
        st.completed = false;
        st.cancelled = false;
        downloads[key] = move(st);
    }

    if (!allocate_file_space(dest_path, file_size)) {
        cout << "ERROR: cannot create destination file\n";
        lock_guard<mutex> lg(downloads_mutex);
        downloads.erase(key);
        return;
    }

    for (int i = 0; i < concurrency; ++i) {
        thread(piece_worker, key, peers, concurrency).detach();
    }
    cout << "Started download: " << file_name << " (" << piece_hashes.size() << " pieces) with concurrency " << concurrency << "\n";
}

void show_downloads() {
    lock_guard<mutex> lg(downloads_mutex);
    if (downloads.empty()) {
        cout << "No downloads in progress.\n";
        return;
    }
    for (auto &p : downloads) {
        auto &d = p.second;
        double pct = d.piece_count ? (100.0 * d.pieces_done / d.piece_count) : 0.0;
        cout << (d.completed ? "[C] " : d.cancelled ? "[X] " : "[ ] ")
             << "[" << d.group_id << "] " << d.file_name
             << " : " << d.pieces_done << "/" << d.piece_count
             << " (" << (int)pct << "%)\n";
    }
}

void stop_download(const string &group_id, const string &file_name) {
    string key = make_key(group_id, file_name);
    lock_guard<mutex> lg(downloads_mutex);
    auto it = downloads.find(key);
    if (it == downloads.end()) {
        cout << "No such download\n";
        return;
    }
    it->second.cancelled = true;
    cout << "Stopped download " << file_name << "\n";
}

// Peer server: serve GET_PIECE <file_name> <piece_index>\n
static void handle_peer_connection(int client_fd) {
    string request;
    char buf[1024];
    ssize_t n;
    while ((n = read(client_fd, buf, sizeof(buf))) > 0) {
        request.append(buf, n);
        if (request.find('\n') != string::npos) break;
    }
    if (request.empty()) { close(client_fd); return; }
    size_t pos = request.find('\n');
    if (pos != string::npos) request = request.substr(0, pos);

    auto trim = [](string s) {
        size_t start = s.find_first_not_of(" \r\n\t");
        size_t end   = s.find_last_not_of(" \r\n\t");
        return (start == string::npos) ? string() : s.substr(start, end - start + 1);
    };
    request = trim(request);

    istringstream iss(request);
    string cmd, file_name;
    int piece_idx = -1;
    iss >> cmd >> file_name >> piece_idx;

    if (cmd != "GET_PIECE" || file_name.empty() || piece_idx < 0) {
        close(client_fd);
        return;
    }

    string path = "./shared/" + file_name;
    FILE *fp = fopen(path.c_str(), "rb");
    if (!fp) { close(client_fd); return; }

    vector<char> buffer(PIECE_SIZE);
    if (fseeko(fp, (off_t)piece_idx * PIECE_SIZE, SEEK_SET) != 0) {
        fclose(fp); close(client_fd); return;
    }

    size_t bytes = fread(buffer.data(), 1, PIECE_SIZE, fp);
    fclose(fp);

    if (bytes > 0) {
        send(client_fd, buffer.data(), bytes, 0);
    }
    close(client_fd);
}

void start_peer_server(const string &bind_ip, int client_port) {
    thread([bind_ip, client_port] {
        int server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) return;
        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(client_port);
        inet_pton(AF_INET, bind_ip.c_str(), &addr.sin_addr);

        if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { close(server_fd); return; }
        if (listen(server_fd, 10) < 0) { close(server_fd); return; }

        while (true) {
            struct sockaddr_in cli{};
            socklen_t clilen = sizeof(cli);
            int cfd = accept(server_fd, (struct sockaddr*)&cli, &clilen);
            if (cfd < 0) continue;
            thread(handle_peer_connection, cfd).detach();
        }
        close(server_fd);
    }).detach();
}

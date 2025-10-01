#pragma once
#include <string>
#include <vector>
#include <map>
#include <mutex>

struct DownloadState {
    std::string group_id;
    std::string file_name;
    std::string dest_path;
    long long file_size = 0;
    int piece_count = 0;
    std::vector<std::string> piece_hashes; // expected hashes per piece
    std::string file_hash;                  // expected full-file hash

    // scheduling / state
    std::vector<int> piece_owner_idx;       // used to try different peers (round-robin)
    std::vector<bool> done;                 // per-piece done flag
    std::vector<bool> in_progress_flags;    // per-piece currently being downloaded
    int pieces_done = 0;                    // how many pieces completed
    int in_progress = 0;                    // how many pieces currently being downloaded
    bool completed = false;
};

extern std::map<std::string, DownloadState> downloads; // key: "<group>:<file>"
extern std::mutex downloads_mutex;

// Start a download. peers: vector of "ip:port" strings.
void start_download(const std::string &group_id,
                    const std::string &file_name,
                    const std::string &dest_path,
                    long long file_size,
                    const std::vector<std::string> &piece_hashes,
                    const std::string &file_hash,
                    const std::vector<std::string> &peers,
                    int concurrency = 4);

// show status lines like: [C] [group id] filename
void show_downloads();

// stop a running download
void stop_download(const std::string &group_id, const std::string &file_name);

// start a small peer-server that serves GET_PIECE <file_name> <piece_idx>\n
// bind_ip and client_port come from client's CLI args
void start_peer_server(const std::string &bind_ip, int client_port);

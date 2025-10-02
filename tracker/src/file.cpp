#include "utils.hpp"
#include "user.hpp"
#include "file.hpp"
#include "sync.hpp"
#include <bits/stdc++.h>
#include <iostream>


using namespace std;


unordered_map<string, unordered_map<string, FileInfo>> shared_files;
extern unordered_map<string, string> user_peer_addresses; 

string upload_file(const string& group_id, const string& user,
                   const string& file_name, long long file_size,
                   const vector<string>& piece_hashes, const string& file_hash) {
    if (user.empty() || !logged_in[user]) {
        return "ERROR: Must be logged in to upload file\n";
    }
    if (groups.find(group_id) == groups.end()) {
        return "ERROR: Group does not exist\n";
    }
    if (!group_members[group_id].count(user)) {
        return "ERROR: Must be group member to upload file\n";
    }

    FileInfo &f = shared_files[group_id][file_name];
    f.owner = user;
    f.file_size = file_size;
    f.piece_hashes = piece_hashes;
    f.file_hash = file_hash;

    // ✅ store peer address instead of username
    string peer_addr = user_peer_addresses[user];
    if (peer_addr.empty()) {
        return "ERROR: No peer address registered for user\n";
    }
    f.peers.insert(peer_addr);

    // ✅ sync everything (size, hash, piece count, peer addr)
    string log = "UPLOAD_FILE " + group_id + " " + file_name + " " + to_string(file_size) +
                 " " + file_hash + " " + to_string(piece_hashes.size());
    for (auto &h : piece_hashes) log += " " + h;
    log += " " + peer_addr;
    sync_write(log);

    return "File uploaded successfully\n";
}


string list_files(const string& group_id, const string& user) {
    if (groups.find(group_id) == groups.end()) {
        return "ERROR: Group does not exist\n";
    }

    // Any group member can list files
    if (!group_members[group_id].count(user)) {
        return "ERROR: Must be group member to view files\n";
    }

    string result = "Files in group " + group_id + ":\n";
    for (auto &p : shared_files[group_id]) {
        result += " - " + p.first + " (owner: " + p.second.owner + ")\n";
    }
    return result;
}

string stop_share(const string& group_id, const string& user, const string& file_name) {
    if (shared_files[group_id].find(file_name) == shared_files[group_id].end()) {
        return "ERROR: File not found\n";
    }

    string peer_addr = user_peer_addresses[user];
    shared_files[group_id][file_name].peers.erase(peer_addr);
    if (shared_files[group_id][file_name].peers.empty()) {
        shared_files[group_id].erase(file_name);
    }
    sync_write("STOP_SHARE " + group_id + " " + file_name + " " + peer_addr);

    return "Stopped sharing " + file_name + "\n";
}

string get_file_info(const string& group_id, const string& file_name, const string& user) {
    if (groups.find(group_id) == groups.end()) {
        return "ERROR: Group does not exist\n";
    }
    if (!group_members[group_id].count(user)) {
        return "ERROR: Must be group member to get file info\n";
    }
    if (shared_files[group_id].find(file_name) == shared_files[group_id].end()) {
        return "ERROR: File not found in group\n";
    }

    const FileInfo &f = shared_files[group_id].at(file_name);
    ostringstream oss;
    oss << "OK " << f.file_size << " " << f.file_hash << " " << f.piece_hashes.size();
    for (auto &h : f.piece_hashes) oss << " " << h;
    oss << " peers:";
    for (auto &p : f.peers) oss << " " << p;
    oss << "\n";
    return oss.str();
}


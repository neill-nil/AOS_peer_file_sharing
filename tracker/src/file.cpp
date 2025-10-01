#include "utils.hpp"
#include "user.hpp"
#include "file.hpp"
#include "sync.hpp"
#include <bits/stdc++.h>
#include <iostream>


using namespace std;


unordered_map<string, unordered_map<string, FileInfo>> shared_files;

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
    f.peers.insert(user);

    sync_write("UPLOAD_FILE " + group_id + " " + file_name + " " + user);

    return "File " + file_name + " uploaded successfully\n";
}

string list_files(const string& group_id, const string& user) {
    if (groups.find(group_id) == groups.end()) {
        return "ERROR: Group does not exist\n";
    }
    if (!group_members[group_id].count(user)) {
        return "ERROR: Must be group member to list files\n";
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

    shared_files[group_id][file_name].peers.erase(user);
    if (shared_files[group_id][file_name].peers.empty()) {
        shared_files[group_id].erase(file_name); // remove file if no peers left
    }

    sync_write("STOP_SHARE " + group_id + " " + file_name + " " + user);

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
    string result = "FILE_INFO " + group_id + " " + file_name + "\n";
    result += "size: " + to_string(f.file_size) + "\n";
    result += "owner: " + f.owner + "\n";
    result += "hash: " + f.file_hash + "\n";
    result += "pieces: " + to_string(f.piece_hashes.size()) + "\n";
    for (size_t i = 0; i < f.piece_hashes.size(); i++) {
        result += " " + f.piece_hashes[i] + "\n";
    }
    result += "peers:\n";
    for (auto &p : f.peers) {
        result += " " + p + "\n";
    }
    return result;
}


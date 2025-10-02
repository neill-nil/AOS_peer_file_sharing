#include "utils.hpp"
#include "user.hpp"
#include "file.hpp"
#include "sync.hpp"
#include <bits/stdc++.h>
#include <iostream>

using namespace std;

// actual definition of shared_files (header declares it extern)
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

    // store the username as seeder (not the ip:port)
    f.seeders.insert(user);

    // UPLOAD_FILE <group> <file> <user> <file_size> <file_hash> <piece_count> <piece_hash1> <piece_hash2> ...
    string log = "UPLOAD_FILE " + group_id + " " + file_name + " " + user + " " +
                 to_string(file_size) + " " + file_hash + " " + to_string(piece_hashes.size());
    for (auto &h : piece_hashes) log += " " + h;
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

    shared_files[group_id][file_name].seeders.erase(user);

    auto &vec = user_seed_history[user]; 
    vec.erase(
        remove_if(vec.begin(), vec.end(),
                  [&](const pair<string,string>& entry) {
                      return entry.first == group_id && entry.second == file_name;
                  }),
        vec.end()
    );

    if (vec.empty()) {
        user_seed_history.erase(user);
    }

    if (shared_files[group_id][file_name].seeders.empty()) {
        shared_files[group_id].erase(file_name);
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

    // Build response in the OK format your client expects:
    ostringstream oss;
    oss << "OK " << f.file_size << " " << f.file_hash << " " << f.piece_hashes.size();
    for (auto &h : f.piece_hashes) oss << " " << h;

    oss << " peers:";
    // convert seeder usernames -> ip:port using user_peer_addresses map
    for (auto &username : f.seeders) {
        auto it = user_peer_addresses.find(username);
        if (it != user_peer_addresses.end() && !it->second.empty()) {
            oss << " " << it->second;
        }
    }
    oss << "\n";
    return oss.str();
}

string announce_seeder(const string& group_id, const string& user, const string& file_name) {
    if (!group_members[group_id].count(user)) {
        return "ERROR: Must be group member to seed file\n";
    }
    if (shared_files[group_id].find(file_name) == shared_files[group_id].end()) {
        return "ERROR: File not found in group\n";
    }

    shared_files[group_id][file_name].seeders.insert(user);

    sync_write("ANNOUNCE_SEEDER " + group_id + " " + file_name + " " + user);

    return "Seeder added for " + file_name + "\n";
}

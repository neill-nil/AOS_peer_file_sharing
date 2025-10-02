#include <utils.hpp>
#include <user.hpp>
#include <file.hpp>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sstream>
#include <iostream>

int sync_fd = -1;

void init_sync_file(const char* filename) {
    sync_fd = open(filename, O_RDWR | O_CREAT, 0666);
    if (sync_fd < 0) {
        handle_error("open sync file");
        exit(1);
    }
}

void sync_write(const string& entry) {
    string line = entry + "\n";
    if (write(sync_fd, line.c_str(), line.size()) < 0) {
        handle_error("write sync file");
    }
    fsync(sync_fd);
}

void replay_sync_file() {
    static off_t last_offset = 0;
    if (sync_fd < 0) return;
    lseek(sync_fd, last_offset, SEEK_SET);

    char buffer[4096];
    string content;
    ssize_t bytes;
    while ((bytes = read(sync_fd, buffer, sizeof(buffer))) > 0) {
        content.append(buffer, bytes);
        last_offset += bytes;
    }

    if (content.empty()) return;

    istringstream iss(content);
    string cmd;
    while (iss >> cmd) {
        if (cmd == "CREATE_USER") {
            string username, password;
            iss >> username >> password;
            users[username] = password;
            logged_in[username] = false;
        } else if (cmd == "CREATE_GROUP") {
            string group_id, owner;
            iss >> group_id >> owner;
            groups[group_id] = owner;
            group_members[group_id].insert(owner);
        } else if (cmd == "JOIN_GROUP") {
            string group_id, user;
            iss >> group_id >> user;
            join_requests[group_id].insert(user);
        } else if (cmd == "ACCEPT_REQUEST") {
            string group_id, user;
            iss >> group_id >> user;
            join_requests[group_id].erase(user);
            group_members[group_id].insert(user);
        } else if (cmd == "LEAVE_GROUP") {
            string group_id, user;
            iss >> group_id >> user;
            group_members[group_id].erase(user);
        } else if (cmd == "LOGIN") {
            string username;
            iss >> username;
            logged_in[username] = true;
        } else if (cmd == "LOGOUT") {
            string username;
            iss >> username;
            logged_in[username] = false;
        } else if (cmd == "CHANGE_OWNER") {
            string group_id, new_owner;
            iss >> group_id >> new_owner;
            groups[group_id] = new_owner;
        } else if (cmd == "DELETE_GROUP") {
            string group_id;
            iss >> group_id;
            groups.erase(group_id);
            group_members.erase(group_id);
            join_requests.erase(group_id);
        } else if (cmd == "UPLOAD_FILE") {
            // New format:
            // UPLOAD_FILE <group_id> <file_name> <username> <file_size> <file_hash> <piece_count> <piece_hash1> ...
            string group_id, file_name, username, file_hash;
            long long file_size = 0;
            int piece_count = 0;
            iss >> group_id >> file_name >> username >> file_size >> file_hash >> piece_count;
            vector<string> piece_hashes;
            for (int i = 0; i < piece_count; ++i) {
                string ph;
                iss >> ph;
                piece_hashes.push_back(ph);
            }

            FileInfo &f = shared_files[group_id][file_name];
            f.owner = username;
            f.file_size = file_size;
            f.file_hash = file_hash;
            f.piece_hashes = piece_hashes;
            f.seeders.insert(username);
        } else if (cmd == "ANNOUNCE_SEEDER") {
            string group_id, file_name, username;
            iss >> group_id >> file_name >> username;
            if (shared_files.count(group_id) && shared_files[group_id].count(file_name)) {
                shared_files[group_id][file_name].seeders.insert(username);
            }
        }
        else if (cmd == "STOP_SHARE") {
            // STOP_SHARE <group_id> <file_name> <username>
            string group_id, file_name, username;
            iss >> group_id >> file_name >> username;
            if (shared_files.count(group_id) && shared_files[group_id].count(file_name)) {
                shared_files[group_id][file_name].seeders.erase(username);
                if (shared_files[group_id][file_name].seeders.empty()) {
                    shared_files[group_id].erase(file_name);
                }
            }
        }
        else if (cmd == "STOP_SHARE_ALL") {
                string username; iss >> username;
                for (auto &group_pair : shared_files) {
                    auto &files = group_pair.second;
                    vector<string> erase_files;
                    for (auto &file_pair : files) {
                        file_pair.second.seeders.erase(username);
                        if (file_pair.second.seeders.empty()) erase_files.push_back(file_pair.first);
                    }
                    for (auto &fn : erase_files) files.erase(fn);
                }
            }

        else {
            // Unrecognized entry — attempt to skip the line safely
            string rest;
            getline(iss, rest);
        }
    }
}

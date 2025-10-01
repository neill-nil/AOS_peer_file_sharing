#include <utils.hpp>
#include <user.hpp>
#include <file.hpp>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sstream>


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
    lseek(sync_fd, 0, SEEK_SET);
    char buffer[4096];
    string content;

    ssize_t bytes;
    while ((bytes = read(sync_fd, buffer, sizeof(buffer))) > 0) {
        content.append(buffer, bytes);
    }

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
            string group_id, file_name, user;
            iss >> group_id >> file_name >> user;
            shared_files[group_id][file_name].owner = user;
            shared_files[group_id][file_name].peers.insert(user);
        }
        else if (cmd == "STOP_SHARE") {
            string group_id, file_name, user;
            iss >> group_id >> file_name >> user;
            shared_files[group_id][file_name].peers.erase(user);
            if (shared_files[group_id][file_name].peers.empty()) {
                shared_files[group_id].erase(file_name);
            }
        }
    }
}


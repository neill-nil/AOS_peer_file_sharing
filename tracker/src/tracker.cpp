#include "user.hpp"
#include "utils.hpp"
#include "sync.hpp"
#include "file.hpp"
#include <iostream>
#include <sstream>
#include <unistd.h>
#include <arpa/inet.h>
#include <map>
#include <thread>
#include <algorithm>
#include <cctype>

using namespace std;

// static string global_current_user;

static string trim(const string &s) {
    size_t a = 0, b = s.size();
    while (a < b && isspace((unsigned char)s[a])) ++a;
    while (b > a && isspace((unsigned char)s[b-1])) --b;
    return s.substr(a, b-a);
}

string handle_command(const string& command, string& current_user) {
    istringstream iss(command);
    string cmd;
    iss >> cmd;

    if (cmd == "create_user") {
        string username, password;
        iss >> username >> password;
        return create_user(username, password);
    } 
    else if (cmd == "login") {
        string username, password, peer_addr;
        iss >> username >> password >> peer_addr;
        return login_user(username, password, current_user, peer_addr);
    }
    else if (cmd == "logout") {
        return logout_user(current_user); // this should also clear current_user
    }
    else if (cmd == "create_group") {
        string group_id;
        iss >> group_id;
        return create_group(group_id, current_user);
    }
    else if (cmd == "join_group") {
        string group_id;
        iss >> group_id;
        return join_group(group_id, current_user);
    }
    else if (cmd == "leave_group") {
        string group_id;
        iss >> group_id;
        return leave_group(group_id, current_user);
    }
    else if (cmd == "list_groups") {
        return list_groups();
    }
    else if (cmd == "get_file_info") {
        string group_id, file_name;
        cout<<"---------------------";
        iss >> group_id >> file_name;
        return get_file_info(group_id, file_name, current_user);
    }
    else if (cmd == "list_requests") {
        string group_id;
        iss >> group_id;
        return list_requests(group_id, current_user);
    }
    else if (cmd == "list_files") {
        string group_id;
        iss >> group_id;
        return list_files(group_id, current_user);
    }
    else if (cmd == "accept_request") {
        string group_id, user;
        iss >> group_id >> user;
        return accept_request(group_id, user, current_user);
    }
    else if (cmd == "upload_file") {
        string group_id, file_path;
        long long file_size;
        iss >> group_id >> file_path >> file_size;

        // strip directory, keep only basename
        string file_name = file_path;
        size_t pos = file_path.find_last_of("/\\");
        if (pos != string::npos) {
            file_name = file_path.substr(pos + 1);
        }

        vector<string> piece_hashes;
        string hash;
        while (iss >> hash) {
            piece_hashes.push_back(hash);
        }
        string file_hash = piece_hashes.back();
        piece_hashes.pop_back();

        return upload_file(group_id, current_user, file_name, file_size, piece_hashes, file_hash);
    }
    else if (cmd == "stop_share") {
        string group_id, file_name;
        iss >> group_id >> file_name;
        return stop_share(group_id, current_user, file_name);
    }

    return "ERROR: Unknown command\n";
}



void handle_peer_request(int client_socket) {
    string current_user;
    char buffer[4096];
    while (true) {
        ssize_t valread = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        if (valread <= 0) break;

        buffer[valread] = '\0';
        string raw(buffer, static_cast<size_t>(valread));
        string command = trim(raw);
        if (command.empty()) continue;

        if (command == "quit") {
            send(client_socket, "Goodbye\n", 8, 0);
            break;
        }

        string response = handle_command(command, current_user);
        if (response.empty()) response = "\n";
        if (response.back() != '\n') response.push_back('\n');
        send_all(client_socket, response.c_str(), response.size());

        if (response.find("Logout successful") != string::npos) {
            current_user.clear();
        }
    }
    if (!current_user.empty()) {
        if (logged_in.find(current_user) != logged_in.end()) {
            logged_in[current_user] = false;
        }
        sync_write("LOGOUT " + current_user);
        cout << "⚠️ Auto-logged out user " << current_user << " due to client disconnect\n";
        current_user.clear();
    }

    close(client_socket);
}
//


void tracker_loop(int server_fd, bool &running) {
    while (running) {
        struct sockaddr_in address;
        socklen_t addrlen = sizeof(address);

        int new_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        if (new_socket < 0) {
            if (errno == EINTR) break;
            perror("accept failed");
            continue;
        }

        // handle each client in its own thread (connection persists here)
        thread t([new_socket]() {
            handle_peer_request(new_socket);
        });
        t.detach();
    }

    close(server_fd);
    cout << "Tracker stopped.\n";
}

int setup_tracker(int port) {
    int server_fd;
    struct sockaddr_in address;
    int opt = 1;

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        handle_error("socket failed", true);
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        handle_error("setsockopt failed", true);
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        handle_error("bind failed", true);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 5) < 0) {
        handle_error("listen failed", true);
        exit(EXIT_FAILURE);
    }

    cout << "Tracker running on port " << port << "...\n";
    return server_fd;
}

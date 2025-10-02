#include "utils.hpp"
#include "file.hpp"
#include "sync.hpp"
#include <iostream>
#include <bits/stdc++.h>

using namespace std;

unordered_map<string, bool> logged_in;
unordered_map<string, string> users; // username -> password
unordered_map<string, string> groups; // group_id -> owner
unordered_map<string, unordered_set<string>> group_members; // group_id -> members
unordered_map<string, unordered_set<string>> join_requests; // group_id -> pending requests
unordered_map<string, string> user_peer_addresses; // username -> ip:port
unordered_map<string, vector<pair<string,string>>> user_seed_history;


// USERS
string create_user(const string& username, const string& password) {
    if (users.find(username) != users.end()) {
        return "ERROR: User already exists\n";
    }
    users[username] = password;
    logged_in[username] = false;
    sync_write("CREATE_USER " + username + " " + password);
    return "User created successfully\n";
}

string login_user(string& current_user, const string& username, const string& password, const string& peer_addr) {
    cerr << "[DEBUG] login_user called for username=" << username 
         << " password=" << password << endl;

    if (!users.count(username)) {
        cerr << "[DEBUG] user not found in users map" << endl;
    } else {
        cerr << "[DEBUG] stored password=" << users[username] << endl;
    }
    if (!users.count(username) || users[username] != password) {
        return "ERROR: Invalid credentials\n";
    }
    if (logged_in[username]) {
        return "ERROR: User already logged in somewhere else\n";
    }

    logged_in[username] = true;
    user_peer_addresses[username] = peer_addr;
    current_user = username;

    //Restore seeding from history
    if (user_seed_history.count(username)) {
        for (auto &entry : user_seed_history[username]) {
            string group_id = entry.first;
            string file_name = entry.second;

            if (shared_files.count(group_id) && shared_files[group_id].count(file_name)) {
                shared_files[group_id][file_name].seeders.insert(username);
                sync_write("ANNOUNCE_SEEDER " + group_id + " " + file_name + " " + username);
            }
        }
    }

    sync_write("LOGIN " + username);
    return "Login successful as " + username + "\n";
}


string logout_user(string& current_user) {
    if (current_user.empty()) {
        return "ERROR: No user logged in\n";
    }

    logged_in[current_user] = false;

    // Save seeding history before removing
    user_seed_history[current_user].clear();
    for (auto &grp : shared_files) {
        for (auto &fpair : grp.second) {
            if (fpair.second.seeders.count(current_user)) {
                user_seed_history[current_user].push_back({grp.first, fpair.first});
                fpair.second.seeders.erase(current_user);
            }
        }
    }

    sync_write("LOGOUT " + current_user);

    string old_user = current_user;
    current_user.clear();

    return "Logout successful (" + old_user + ")\n";
}


// GROUPS
string create_group(const string& group_id, string& current_user) {
    if (current_user.empty() || !logged_in[current_user]) {
        return "ERROR: Must be logged in to create group\n";
    }
    if (groups.find(group_id) != groups.end()) {
        return "ERROR: Group already exists\n";
    }
    groups[group_id] = current_user;
    group_members[group_id].clear();
    group_members[group_id].insert(current_user);
    sync_write("CREATE_GROUP " + group_id + " " + current_user);
    return "Group created successfully\n";
}

string join_group(const string& group_id, string& current_user) {
    if (current_user.empty() || !logged_in[current_user]) {
        return "ERROR: Must be logged in to join a group\n";
    }
    if (groups.find(group_id) == groups.end()) {
        return "ERROR: Group does not exist\n";
    }
    if (groups[group_id] == current_user) {
        return "ERROR: You are the owner of this group\n";
    }
    if (group_members[group_id].count(current_user)) {
        return "ERROR: Already a member of this group\n";
    }
    if (join_requests[group_id].count(current_user)) {
        return "ERROR: Join request already sent\n";
    }

    join_requests[group_id].insert(current_user);
    sync_write("JOIN_GROUP " + group_id + " " + current_user);
    return "Join request sent\n";
}

string leave_group(const string& group_id, string& current_user) {
    if (current_user.empty() || !logged_in[current_user]) {
        return "ERROR: Must be logged in to leave a group\n";
    }

    if (groups.find(group_id) == groups.end()) {
        return "ERROR: Group does not exist\n";
    }

    // If owner leaves
    if (groups[group_id] == current_user) {
        // remove owner from members first
        group_members[group_id].erase(current_user);

        if (group_members[group_id].empty()) {
            // no members left, delete group
            groups.erase(group_id);
            group_members.erase(group_id);
            join_requests.erase(group_id);
            sync_write("DELETE_GROUP " + group_id);
            return "Group deleted as owner left\n";
        } else {
            // reassign owner to first available member
            string new_owner = *group_members[group_id].begin();
            groups[group_id] = new_owner;
            // ensure new owner remains in members set (it is)
            sync_write("CHANGE_OWNER " + group_id + " " + new_owner);
            return "Owner left, new owner is " + new_owner + "\n";
        }
    }

    // Normal member leaving
    if (group_members[group_id].erase(current_user)) {
        sync_write("LEAVE_GROUP " + group_id + " " + current_user);
        return "Left group successfully\n";
    }
    return "ERROR: Not a member of group\n";
}

string list_groups() {
    string result = "Groups:\n";
    for (auto& g : groups) {
        result += " - " + g.first + " (owner: " + g.second + ")\n";
    }
    return result;
}

string list_requests(const string& group_id, string& current_user) {
    if (groups.find(group_id) == groups.end()) {
        return "ERROR: Group does not exist\n";
    }
    if (groups[group_id] != current_user) {
        return "ERROR: Only group owner can view requests\n";
    }
    string result = "Requests for " + group_id + ":\n";
    for (auto& u : join_requests[group_id]) {
        result += " - " + u + "\n";
    }
    return result;
}

string accept_request(const string& group_id, const string& user, string& current_user) {
    if (current_user.empty() || !logged_in[current_user]) {
        return "ERROR: Must be logged in to accept requests\n";
    }

    if (groups.find(group_id) == groups.end()) {
        return "ERROR: Group does not exist\n";
    }
    if (groups[group_id] != current_user) {
        return "ERROR: Only owner can accept requests\n";
    }
    if (join_requests[group_id].erase(user)) {
        group_members[group_id].insert(user);
        sync_write("ACCEPT_REQUEST " + group_id + " " + user);
        return "Request accepted for user " + user + "\n";
    }
    return "ERROR: No such request\n";
}

#include "utils.hpp"
#include "sync.hpp"
#include <iostream>
#include <bits/stdc++.h>

using namespace std;

unordered_map<string, bool> logged_in;
unordered_map<string, string> users;
unordered_map<string, string> groups;
unordered_map<string, unordered_set<string>> group_members;
unordered_map<string, unordered_set<string>> join_requests;


// USERS
string create_user(string& username, string& password) {
    if (users.find(username) != users.end()) {
        return "ERROR: User already exists\n";
    }
    users[username] = password;
    logged_in[username] = false;
    sync_write("CREATE_USER " + username + " " + password);
    return "User created successfully\n";
}

string login_user(string& username, string& password, string& current_user) {
    auto it = users.find(username);
    if (it == users.end() || it->second != password) {
        return "ERROR: Invalid credentials\n";
    }
    logged_in[username] = true;
    current_user = username;
    sync_write("LOGIN " + username);
    return "Login successful\n";
}

string logout_user(string& current_user) {
    if (current_user.empty()) return "ERROR: No user logged in\n";
    logged_in[current_user] = false;
    sync_write("LOGOUT " + current_user);
    return "Logout successful\n";
}



// GROUPS
string create_group(string& group_id, string& current_user) {
    if (current_user.empty() || !logged_in[current_user]) {
        return "ERROR: Must be logged in to create group\n";
    }
    if (groups.find(group_id) != groups.end()) {
        return "ERROR: Group already exists\n";
    }
    groups[group_id] = current_user;
    group_members[group_id].insert(current_user);
    sync_write("CREATE_GROUP " + group_id + " " + current_user);
    return "Group created successfully\n";
}

string join_group(string& group_id, string& current_user) {
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


string leave_group(string& group_id, string& current_user) {
    if (current_user.empty() || !logged_in[current_user]) {
        return "ERROR: Must be logged in to leave a group\n";
    }

    if (groups.find(group_id) == groups.end()) {
        return "ERROR: Group does not exist\n";
    }

    // If owner leaves
    if (groups[group_id] == current_user) {
        group_members[group_id].erase(current_user);
        if (group_members[group_id].empty()) {
            // no members left, delete group
            groups.erase(group_id);
            group_members.erase(group_id);
            join_requests.erase(group_id);
            sync_write("DELETE_GROUP " + group_id);
            return "Group deleted as owner left\n";
        } else {
            // pick a valid member as new owner
            auto it = group_members[group_id].begin();
            string new_owner;
            while (it != group_members[group_id].end()) {
                if (!it->empty() && *it != "JOIN_GROUP") { 
                    new_owner = *it;
                    break;
                }
                ++it;
            }
            if (new_owner.empty()) {
                return "ERROR: Could not find valid new owner\n";
            }

            groups[group_id] = new_owner;
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

string list_requests(string& group_id, string& current_user) {
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

string accept_request(string& group_id, string& user, string& current_user) {
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

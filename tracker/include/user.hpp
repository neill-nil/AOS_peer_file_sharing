#ifndef USER_HPP
#define USER_HPP

#include <unordered_map>
#include <unordered_set>
#include <string>
using namespace std;

extern unordered_map<string, string> users;
extern unordered_map<string, bool> logged_in;
extern unordered_map<string, string> groups;
extern unordered_map<string, unordered_set<string>> group_members;
extern unordered_map<string, unordered_set<string>> join_requests;
extern unordered_map<string, string> user_peer_addresses;


string create_user(const string& username, const string& password);
string login_user(const string& username, const string& password,
                  string& current_user, const string& peer_addr);
string logout_user(string& current_user);
string create_group(const string& group_id, string& current_user);
string join_group(const string& group_id, string& current_user);
string leave_group(const string& group_id, string& current_user);
string list_groups();
string list_requests(const string& group_id, string& current_user);
string accept_request(const string& group_id, const string& user, string& current_user);

#endif


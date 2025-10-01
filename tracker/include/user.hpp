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


string create_user(string& username, string& password);
string login_user(string& username, string& password, string& current_user);
string logout_user(string& current_user);
string create_group(string& group_id, string& current_user);
string join_group(string& group_id, string& current_user);
string leave_group(string& group_id, string& current_user);
string list_groups();
string list_requests(string& group_id, string& current_user);
string accept_request(string& group_id, string& user, string& current_user);

#endif


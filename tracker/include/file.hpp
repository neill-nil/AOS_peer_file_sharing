#pragma once
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
using namespace std;

struct FileInfo {
    string owner;
    string file_path;
    long long file_size;
    vector<string> piece_hashes;
    string file_hash;
    unordered_set<string> peers;
};

extern unordered_map<string, unordered_map<string, FileInfo>> shared_files;

string upload_file(const string& group_id, const string& user,
                   const string& file_name, long long file_size,
                   const vector<string>& piece_hashes, const string& file_hash);

string list_files(const string& group_id, const string& user);
string stop_share(const string& group_id, const string& user, const string& file_name);
string get_file_info(const string& group_id, const string& file_name, const string& user);

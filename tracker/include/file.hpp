#pragma once
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace std;

struct FileInfo {
    string owner;                      // uploader username
    string file_path;                  // optional: can be used locally
    long long file_size = 0;
    vector<string> piece_hashes;
    string file_hash;
    unordered_set<string> seeders;     // store usernames of seeders
};

// Global map: group_id -> (file_name -> FileInfo)
extern unordered_map<string, unordered_map<string, FileInfo>> shared_files;

// API
string upload_file(const string& group_id, const string& user,
                   const string& file_name, long long file_size,
                   const vector<string>& piece_hashes, const string& file_hash);

string list_files(const string& group_id, const string& user);
string stop_share(const string& group_id, const string& user, const string& file_name);
string get_file_info(const string& group_id, const string& file_name, const string& user);
string announce_seeder(const string& group_id, const string& user, const string& file_name);
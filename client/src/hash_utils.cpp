#include "hash_utils.hpp"
#include <openssl/sha.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

using namespace std;

string sha1_hex(const char* data, size_t len) {
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(data), len, hash);
    char buf[41];
    for (int i = 0; i < 20; i++)
        sprintf(buf + (i * 2), "%02x", hash[i]);
    buf[40] = '\0';
    return string(buf);
}

string preprocess_upload_file(const string& input) {
    istringstream iss(input);
    string cmd, group_id, file_path;
    iss >> cmd >> group_id >> file_path;

    ifstream in(file_path, ios::binary);
    if (!in) {
        cerr << "ERROR: Cannot open file " << file_path << "\n";
        return "";
    }

    const size_t PIECE_SIZE = 512 * 1024;
    vector<string> piece_hashes;
    vector<char> buffer(PIECE_SIZE);
    long long total_size = 0;

    while (in) {
        in.read(buffer.data(), PIECE_SIZE);
        streamsize n = in.gcount();
        if (n > 0) {
            piece_hashes.push_back(sha1_hex(buffer.data(), n));
            total_size += n;
        }
    }
    in.close();

    // compute full file hash
    ifstream in2(file_path, ios::binary);
    SHA_CTX ctx;
    SHA1_Init(&ctx);
    vector<char> buf2(PIECE_SIZE);
    while (in2) {
        in2.read(buf2.data(), PIECE_SIZE);
        streamsize n = in2.gcount();
        if (n > 0) SHA1_Update(&ctx, buf2.data(), n);
    }
    unsigned char final_hash[SHA_DIGEST_LENGTH];
    SHA1_Final(final_hash, &ctx);
    char fullbuf[41];
    for (int i = 0; i < 20; i++) sprintf(fullbuf + i*2, "%02x", final_hash[i]);
    fullbuf[40] = '\0';
    string full_hash(fullbuf);
    in2.close();

    // build expanded command
    ostringstream oss;
    oss << "upload_file " << group_id << " " << file_path << " " << total_size;
    for (auto &h : piece_hashes) oss << " " << h;
    oss << " " << full_hash;
    system("mkdir -p ./shared");
    string dest = "./shared/" + file_path.substr(file_path.find_last_of("/\\") + 1);
    ifstream src(file_path, ios::binary);
    ofstream dst(dest, ios::binary);
    dst << src.rdbuf();
    return oss.str();
}

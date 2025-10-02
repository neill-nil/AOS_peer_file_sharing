#include <arpa/inet.h>
#include <iostream>
#include <cstring> 

using namespace std;

void handle_error(const string& msg, bool use_errno = false) {
    if (use_errno) {
    cerr << "[ERROR] " << msg << ": " << strerror(errno) <<endl;
    } else {
    cerr << "[ERROR] " << msg << endl;
    }
}   

ssize_t send_all(int sock, const char* buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t n = send(sock, buf + total, len - total, 0);
        if (n <= 0) return n;
        total += n;
    }
    return total;
}
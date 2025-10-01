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
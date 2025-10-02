#pragma once
#include <string>
#include <vector>

using namespace std;


void handle_error(const string& msg, bool use_errno = false);
ssize_t send_all(int sock, const char* buf, size_t len);

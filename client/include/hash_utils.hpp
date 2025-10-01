#pragma once
#include <string>

std::string sha1_hex(const char* data, size_t len);

std::string preprocess_upload_file(const std::string& input);

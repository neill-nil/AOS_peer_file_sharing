#pragma once
#include <string>
#include <vector>

using namespace std;


void init_sync_file(const char* filename);
void sync_write(const string& entry);
void replay_sync_file();

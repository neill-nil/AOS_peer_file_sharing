#pragma once
#include <string>
#include <vector>

using namespace std;


int setup_tracker(int port);
void handle_peer_request(int client_socket);
void tracker_loop(int server_fd, bool &running) ;
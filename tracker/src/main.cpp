#include "tracker.hpp"
#include "sync.hpp"
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <thread>
#include <csignal>
#include <cstdlib>

using namespace std;

bool running = true;
int tracker_fd_global = -1;

// Shut down on ctrl+C
void signal_handler(int signum) {
    static bool already = false;
    if (!already) {
        cout << "\nShutting down tracker..." << endl;
        running = false;
        if (tracker_fd_global != -1) {
            close(tracker_fd_global);
        }
        already = true;
    }
}

string get_tracker_info(const string& filename, int tracker_no) {
    int fd = open(filename.c_str(), O_RDONLY);
    if (fd < 0) {
        throw runtime_error("Failed to open " + filename);
    }

    string line;
    char c;
    int current = 1;

    while (read(fd, &c, 1) == 1) {
        if (c == '\n') {
            if (current == tracker_no) {
                close(fd);
                return line;
            }
            line.clear();
            current++;
        } else {
            line.push_back(c);
        }
    }

    close(fd);

    if (current == tracker_no && !line.empty()) {
        return line;
    }

    throw runtime_error("Tracker number out of range in " + filename);
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);

    if (argc != 3) {
        cerr << "Usage: " << argv[0] << " <tracker_info_file> <tracker_no>\n";
        return 1;
    }

    string info_file = argv[1];
    int tracker_no = atoi(argv[2]);

    string tracker_entry = get_tracker_info(info_file, tracker_no);
    size_t colon = tracker_entry.find(':');
    if (colon == string::npos) {
        cerr << "Invalid format in tracker_info_file: " << tracker_entry << "\n";
        return 1;
    }

    int port = stoi(tracker_entry.substr(colon + 1));

    string sync_file = "sync.log";  // shared file for both trackers
    init_sync_file(sync_file.c_str());
    replay_sync_file();

    int tracker_fd = setup_tracker(port);
    tracker_fd_global = tracker_fd;
    thread tracker_thread(tracker_loop, tracker_fd, ref(running));

    thread sync_thread([] {
        while (running) {
            this_thread::sleep_for(chrono::seconds(2));
            replay_sync_file();
        }
    });

    // Console thread to allow "quit"
    thread console_thread([] {
        string line;
        while (running && getline(cin, line)) {
            if (line == "quit") {
                cout << "Shutting down tracker (via console)..." << endl;
                running = false;
                if (tracker_fd_global != -1) {
                    close(tracker_fd_global); // unblock accept()
                }
                break;
            }
        }
    });

    while (running) {
        this_thread::sleep_for(chrono::seconds(1));
    }

    close(tracker_fd);
    tracker_thread.join();
    sync_thread.join();
    console_thread.join();

    cout << "Tracker stopped." << endl;
    return 0;
}

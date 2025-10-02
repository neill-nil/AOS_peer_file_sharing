# Peer to Peer File Distribution System 

This project implements a **file distribution system** with exactly **two trackers** that maintain synchronized state.  
The trackers handle **user registration, authentication, group management, and membership requests** while ensuring consistency across both servers. It ensures uploads and downloads of files in pieces/chunks across **multiple peers**. 

## Approach

- **Socket Programming (C++ system calls)**  
  Used `socket`, `bind`, `listen`, `accept`, `read`, `write`, `close` for low-level client–server communication.  

- **Two Trackers for Fault Tolerance**  
  System continues operating as long as at least one tracker is online.  

- **Shared Sync File (`sync.log`)**  
  All state-changing commands (`create_user`, `login`, `create_group`, `join_group`, `leave_group`, `accept_request`, `logout`, `upload_file`, `stop_share`, `announce_seeder`) are written to a common log.  
  Both trackers periodically **replay this file** to stay consistent.  

- **Global State Maps**  
  State is maintained in in-memory maps:  
  `users`, `logged_in`, `groups`, `group_members`, `join_requests`, `shared_files`, `user_seed_history`.  

- **Seeder Management**  
  - A user who uploads a file becomes its seeder.  
  - Any user who downloads a file automatically becomes a seeder.  
  - On `logout`, seeders are temporarily removed but restored on next login.  
  - `stop_share` permanently removes a user from the seeder list for a file.  

- **Concurrent Downloads**  
  - A client can download multiple files simultaneously.  
  - Each file is split into chunks and downloaded concurrently from multiple peers.  
  - Scheduling is **round-robin across available peers** for load balancing.  

- **Session Synchronization**  
  Login/logout states are synchronized across trackers. Seeder lists are also kept consistent.  

- **Minimal Client**  
  A simple client connects to trackers, sends commands, and prints responses.  Automatic **tracker failover** is supported.

## How to Run

### Tracker  
1. Create `tracker_info.txt` with two entries:  
127.0.0.1:9000  
127.0.0.1:9001  

2. Go to each directory (Tracker and Client) and run `make` to compile

3. Run each tracker in separate terminals (after going into `tracker` directory):  
```

./tracker_app tracker_info.txt 1
./tracker_app tracker_info.txt 2

```

### Client  
1. Start client by first going into `client` directory and then giving initial tracker and info file:  
```

./client_app 127.0.0.1:5000 tracker_info.txt

```

2. If one tracker fails, client automatically switches to the other.  

3. Enter commands such as:  
- `create_user alice pass`  
- `login alice pass`  
- `create_group g1`  
- `join_group g1`  
- `list_groups`  
- `upload_file g1 sample.txt`  
- `list_files g1`  
- `download_file g1 sample.txt ./downloaded.txt`  
- `show_downloads`  
- `stop_share g1 sample.txt`  
- `logout`  

## Outcome
This design ensures **high availability, consistency, and fault tolerance** using a shared log for synchronization, while supporting **multi-source concurrent downloads** with automatic seeder management.
```

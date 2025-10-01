# Peer to Peer File Distribution System 

This project implements a **distributed tracker system** with exactly **two trackers** that maintain synchronized state.  
The trackers handle **user registration, authentication, group management, and membership requests** while ensuring consistency across both servers.  

## Approach

- **Socket Programming (C++ system calls)**  
  Used `socket`, `bind`, `listen`, `accept`, `read`, `write`, `close` for low-level client–server communication.  

- **Two Trackers for Fault Tolerance**  
  System continues operating as long as at least one tracker is online.  

- **Shared Sync File (`sync.log`)**  
  All state-changing commands (`create_user`, `login`, `create_group`, `join_group`, `leave_group`, `accept_request`, `logout`) are written to a common log.  
  Both trackers periodically **replay this file** to stay consistent.  

- **Global State Maps**  
  State is maintained in in-memory maps:  
  `users`, `logged_in`, `groups`, `group_members`, and `join_requests`.  

- **Session Synchronization**  
  Login/logout states are synchronized so authentication is valid across trackers.  

- **Minimal Client**  
  A simple client connects to trackers, sends commands, and prints responses.  


## How to Run

### Tracker  
1. Create `tracker_info.txt` with two entries:  
127.0.0.1:9000
127.0.0.1:9001

2. Run each tracker in separate terminals (after going into `tracker` directory):  
./tracker_app tracker_info.txt 1
./tracker_app tracker_info.txt 2


### Client  
1. Start client by first going into `client` directory and then giving initial tracker and info file:  
./client_app 127.0.0.1:9000 tracker_info.txt

2. If one tracker fails, client automatically switches to the other.  

3. Enter commands such as:  
- `create_user jeff pass`  
- `login jeff pass`  
- `create_group g1`  
- `join_group g1`  
- `list_groups`  


## Outcome
This design ensures **high availability, consistency, and fault tolerance** using a simple shared log for synchronization.  

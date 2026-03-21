# LibChat (C++)

LibChat is a modular C++ framework designed as a foundation for building custom chat platforms. It currently supports multi-client, room-based communication over local networks with a current focus on clean architecture and thread-safe state managemtn. LibChat supports communication across different networks when used with a secure overlay network like tailscale.

## Build and Run
``` sh
# compile 
make

# start the server (listens on 8080)
make run_server

#in another shell, start client instance
make run_client
```
## Commands
``` sh
#list all commands
/help

#join a new room
/join <room>

#list all active rooms
/rooms

#see active users in your room
/who

#exit
/quit

```
# Notes
- I am current working on compatibility issues on MacOS, this project was built on and for Linux systems and hasn't been attempted to be ran on windows as of yet (3/20/2026).
- "/help" functionality is still a work in progress.

# Remark
- If you are trying to run the program and it says the address is already in use run:
``` sh
ss -ltnp | grep 8080
```
and kill whatever process is using the address with 
``` sh
kill <PID>
```

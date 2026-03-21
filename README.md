# LibChat (C++)

This is a project inspired by my original Chat App which you can see [here.](https://github.com/alancw2/chat-app)

## Build and Run
``` sh
# compile 
make

# start the server (listens on 8080)
make run_server

#in another shell, start client instance
make run_client
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

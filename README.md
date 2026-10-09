# LibChat (C++)

LibChat is a modular C++ framework designed as a foundation for building custom chat platforms. It currently supports multi-client, room-based communication over local networks with a current focus on clean architecture and thread-safe state managemtn. LibChat supports communication across different networks when used with a secure overlay network like tailscale.

## Build and Run
``` sh
# compile 
make

# start the server (listens on 8080, saves chat history to libchat.db)
make run_server

# or choose where chat history is saved
./server /path/to/history.db

#in another shell, start client instance
make run_client
```
Messages sent to the server must end with a newline (`\n`). The command-line client and the Qt GUI already do this; a custom client needs to as well.

## Rooms and chat history
Each room is separate: chat messages only go to people in the same room. The server saves rooms and chat messages to a SQLite file (`libchat.db` by default), so each room's recent history survives server restarts. Commands and join/leave notices aren't saved.

### 24-hour retention
Messages are kept for **24 hours**, then deleted, so the database doesn't keep growing:
- `/history` and the GUI only show messages from the last 24 hours.
- The server deletes expired messages when it starts and then once a minute, even when nobody is chatting. A message may stay in the file for up to a minute after it expires, but it's never shown.
- A room is deleted once it has had no messages for 24 hours (or 24 hours after it was created, if nobody ever wrote in it). `lobby` is never deleted. If people are still in a deleted room, it's saved again on the next message or `/join`.
- Deleting rows normally leaves the file the same size. The database uses SQLite's incremental auto-vacuum, so space freed by deletions is given back and the file stays roughly as big as one day of messages. An older database file is switched over automatically the first time the server opens it; that's a one-time full `VACUUM`, which can take a moment on a big file.

The 24-hour period is `ChatHistory::DEFAULT_RETENTION_SECONDS` in `include/ChatHistory.hpp`. `ChatHistory` also accepts a different period in its constructor.

- **Chat GUI:** clicking a room in the sidebar switches to it. The chat clears and loads that room's most recent 50 messages from the last 24 hours, with their original times and *Today* / *Yesterday* / date dividers, then shows new messages as they arrive. Like Discord, you only see the room you're in.
- **Terminal client:** type `/history` to show the current room's recent messages.

If the database can't be opened, the server still runs but doesn't save history, and `/history` returns nothing.

### Protocol
`/history` replies with the current room's newest messages, oldest first:
```
~history-begin <more> <room>
~history <unix seconds> <sender>: <body>
~history-end <room>
```
`<more>` is `1` if the room has older messages (from the last 24 hours) than the ones sent. History lines start with `~`, which no nickname or sender label can, so a client can't mistake them for ordinary chat lines. The server answers each client's commands in order, so sending `/join dev` then `/history` returns `dev`'s history.

### Database
Messages are stored in the `messages` table: `id`, `room_id`, `user_id`, `sender`, `body`, `created_at`. `sender` is the name shown when the message was sent. `user_id` is optional, because chat users don't have accounts yet. A room is saved in `rooms` when someone joins it or sends a message in it. Deleting a user keeps their messages, with `user_id` set to `NULL`. Expired messages are deleted by age using an index on `messages.created_at`.

The schema version is stored in the file (currently 3). Older databases are upgraded automatically when the server opens it, and messages that are still within 24 hours are kept.

## GUIs
Both GUIs need Qt 6. On macOS, install it with `brew install qtbase`; the full `qt` package works too but is much larger. Elsewhere, point `QT_PREFIX` at your Qt install, e.g. `make gui QT_PREFIX=/path/to/Qt/6.x/macos`.

``` sh
# chat client GUI: connects to a running server
make run_gui

# debug GUI: runs a server and test clients in one window
make run_debug_gui

# build without running
make gui
make debug_gui
```

### Debug GUI
The debug GUI lets you test LibChat without starting `./server` and a separate client in different terminals. One window contains:
- **A built-in server.** It runs the same server code as `./server` and starts on port 8080 when the app opens. Use the toolbar to stop or restart it, change the port (0 picks a free one), or tick **Allow other machines** so clients on other computers can connect.
- **A live server log.** It shows every connect, disconnect and line received, labelled with the client's socket and name.
- **Test clients.** The app opens with two; **+ Add client** adds more. Each client connects to the built-in server and sets the nickname in its name box (`user1`, `user2`, …) by itself. Each has buttons for `/nick`, `/join`, `/who`, `/rooms`, `/help` and `/quit`, and a transcript of everything sent (→) and received (←).

Transcripts and the log show hidden characters such as `\r`, `\t` and `\x00`, so bad message formatting is easy to spot. Untick **Add \n** on a client to send text without a trailing newline and test how the server handles partial messages.

The built-in server keeps chat history in memory while the app is open, with the same 24-hour retention. History survives **Stop server** / **Start server**, but not quitting the app. Each client has a `/history` button that shows the raw history reply.

The built-in server only listens on `127.0.0.1` unless **Allow other machines** is ticked. You can also point the regular chat GUI or `make run_client` at `127.0.0.1` and the port shown in the toolbar.
## Testing
All tests build into one program, `./tests`. You need the SQLite development library (macOS ships with it; on Debian/Ubuntu install `libsqlite3-dev`).

``` sh
# build ./tests and run every test
make tests

# after building, run only the tests whose names contain a word
./tests bug_002
./tests db_

# remove build output, including ./tests
make clean
```
Each test prints `[PASS]` or `[FAIL]`, followed by a summary like `58/58 tests passed`. A failing test prints the `CHECK` that failed and its file and line. `make tests` exits with a non-zero status if any test fails, so you can use it in scripts and CI. (`make test` does the same thing.)

The tests live in `test/`:
- **`test/DatabaseTest.cpp`** (`db_*`): checks the SQLite schema. That covers the expected columns, the schema version and upgrading a v1 database, the automatic `lobby` room, reopening an existing file, uniqueness and `NULL` rules, foreign keys and deletes, guest messages, finding expired sessions, and the error when a database can't be opened.
- **`test/HistoryTest.cpp`** (`history_*`): checks that `/history` returns the current room's messages (and only those) in the right format, that commands and notices aren't saved, and that history survives a server restart. It also covers the 24-hour retention: expired messages are hidden and deleted (including by the running server by itself), unused rooms are removed but `lobby` is kept, joined rooms are stored, and deleted space is given back to the file system.
- **`test/ServerTest.cpp`** (`server_*`): checks stopping the server and its activity log, which the debug GUI uses.
- **`test/BugFixTest.cpp`** (`bug_*`): one or more tests for every fixed bug, so it can't come back. Each bug is described in [BUGFIXES.md](BUGFIXES.md).

Tests that need a server start a real one on a free port on `127.0.0.1`, inside a separate process (`./tests` starts itself again with `--child <test name>` to run just that test). If the server crashes or hangs, that test fails and the rest still run. The database tests mostly use an in-memory database; the reopening test uses a temporary file in `/tmp` and deletes it.

To add a test, write it in any file in `test/` with the `TEST` macro (new files also go in `TEST_SRCS` in the `Makefile`):
``` cpp
#include "Test.hpp"

TEST(my_new_test) {
    CHECK(1 + 1 == 2);
}
```
When you fix a bug, follow the steps at the top of [BUGFIXES.md](BUGFIXES.md): add a `bug_<number>_...` test that fails without the fix, then add an entry to that file.

To look at a database file by hand, use the `sqlite3` command-line tool (replace `libchat.db` with your file's path):
``` sh
sqlite3 libchat.db ".schema"
sqlite3 libchat.db "SELECT * FROM rooms;"
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

#show your room's recent messages
/history

#exit
/quit

```
# Notes
- LibChat is built on and for UNIX systems, WSL likely works however it has not been debugged by contributors thus far.
- LibChat has a TUI in progress that can be seen and messed around with by checking out the "basic-ui" branch.

# Remark
- If you are trying to run the program and it says the address is already in use run:
``` sh
ss -ltnp | grep 8080
```
and kill whatever process is using the address with 
``` sh
kill <PID>
```

## Author
**Alan Ward**\
UIUC Mathematics \
Github: https://github.com/alancw2

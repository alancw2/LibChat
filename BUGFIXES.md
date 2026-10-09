# Bug Fixes

This file records every bug fixed in LibChat: what went wrong, why, how it was fixed, and which tests stop it from coming back.

## Adding a new entry

When you fix a bug:

1. **Write a test that fails without your fix.** Add it to `test/BugFixTest.cpp` (or a new file in `test/`, listed in `TEST_SRCS` in the `Makefile`). Name it `bug_<number>_<what_it_checks>`, using the next free number below:
   ``` cpp
   TEST(bug_007_short_description) {
       CHECK(...);
   }
   ```
   Tests that need a running server should use `Harness::isolated` and `Harness::startServer()` from `test/ServerHarness.hpp`. Each one then runs in its own process, so a crash fails that test instead of stopping the whole run.
2. **Fix the bug.** Then run `make tests` and make sure everything passes.
3. **Check that the test catches the bug.** Temporarily undo your fix and confirm the new test fails.
4. **Add an entry at the end of this file** using this template:

``` markdown
## BUG-XXX: One-line summary

- **Date fixed:** YYYY-MM-DD
- **Files:** `path/to/file.cpp`
- **Tests:** `./tests bug_XXX`

**Symptom:** What users saw.

**Cause:** Why it happened.

**Fix:** What changed.
```

Run a single bug's tests with `./tests bug_<number>` (after `make tests` has built `./tests`).

---

## BUG-001: Client names contained hidden NUL bytes and garbage

- **Date fixed:** 2026-10-09
- **Files:** `src/Protocol.cpp`, `src/Server.cpp`
- **Tests:** `./tests bug_001`

**Symptom:** Until a user set a nickname, their messages were labelled with something like `127.0.0.1\0\0\x01...@u...:50407` instead of `127.0.0.1:50407`. The extra bytes showed up as junk in terminals, could cut lines short, and broke the GUI's `sender: text` parsing. It was most visible with several clients on the same machine as the server, because they all share the `127.0.0.1` prefix.

**Cause:** `get_peer_ip_address` passed the `char ipstr[INET6_ADDRSTRLEN]` array straight to `std::format`. `std::format` prints a `char` array at its full length (46 bytes), not up to the first NUL, so the label included everything left in the array after the address. The function also left `port` and `ipstr` uninitialized for IPv6 clients.

**Fix:** The new `Protocol::formatPeerAddress` converts the address to `std::string` before formatting. It also handles IPv6 (`[::1]:8080`) and returns an empty string for unknown address types.

## BUG-002: Messages sent close together were merged or dropped

- **Date fixed:** 2026-10-09
- **Files:** `src/Protocol.cpp`, `src/Server.cpp`, `src/Client.cpp`
- **Tests:** `./tests bug_002`

**Symptom:** Commands sent close together were lost. For example, the GUI sends `/nick alice` then `/join dev`, and the `/join` was silently dropped. A paste of two lines was broadcast as one message, with the second line missing the `sender:` prefix. Messages could also be cut in half. This happened most often when the client was on the same machine as the server, because loopback delivers data fast enough for several messages to land in one `recv()`.

**Cause:** TCP is a stream of bytes, not separate messages. The server treated each `recv()` call as exactly one message, but one `recv()` can return several messages or part of one. Commands were detected with `starts_with` on the whole buffer, so only the first command in a batch ran. The command-line client didn't send a newline at all, so the server had no way to tell where one message ended and the next began.

**Fix:**
- `Protocol::LineBuffer` collects incoming bytes and returns one complete line at a time, removing `\r\n` or `\n`. Lines longer than 4096 bytes are split, so a client that never sends a newline can't use up memory.
- The server handles every complete line separately.
- The command-line client now ends each message with `\n`. **Clients must now end every message with a newline;** the server waits for it before processing. The Qt GUI already did this.
- `/join` now trims the room name before announcing it, so join messages no longer contain a stray newline.

## BUG-003: Server crashed when a client disconnected abruptly

- **Date fixed:** 2026-10-09
- **Files:** `src/Server.cpp`, `src/ConnectionHandler.cpp`, `src/main.cpp`
- **Tests:** `./tests bug_003`

**Symptom:** If a client's connection was reset (killed process, network drop, laptop sleep), the whole server could exit. Everyone else was disconnected and messages stopped being delivered.

**Cause:** There were two separate crashes:
1. After `recv()` returned `-1` (e.g. "connection reset"), the server built `std::string(buffer, bytes)` *before* checking for the error. With `bytes == -1` this throws `std::length_error` in a detached thread, which aborts the process.
2. Writing to a socket whose client is gone raises `SIGPIPE`, and by default that signal kills the process.

**Fix:** The server now checks the result of `recv()` before using the data, and retries on `EINTR`. Sockets use `SO_NOSIGPIPE` (macOS) and `send()` uses `MSG_NOSIGNAL` (Linux), so writing to a closed client just fails and logs an error. As a backstop, `main()` also ignores `SIGPIPE`. `accept()` failures from `EINTR` or `ECONNABORTED` no longer stop the server.

## BUG-004: Invalid nicknames were announced as changed

- **Date fixed:** 2026-10-09
- **Files:** `src/Server.cpp`, `src/Commands/Commands.cpp`, `include/Commands.hpp`
- **Tests:** `./tests bug_004`

**Symptom:** `/nick bad!!name` broadcast "X has changed their nickname to bad!!name" even though the nickname wasn't changed. The GUI kept showing the rejected name, because the server's errors (`nickname is taken.`) didn't start with `Nickname`, the prefix the GUI uses to detect a rejected nickname.

**Cause:** `Commands::setNewNickname` returned `void`, so the server couldn't tell when the format check rejected a name, and announced the change anyway.

**Fix:** `setNewNickname` now returns whether it succeeded. A rejected nickname gets `Nickname change failed: invalid nickname.` and a taken one gets `Nickname is taken.`, and neither is broadcast.

## BUG-005: Shared client list was used without locking

- **Date fixed:** 2026-10-09
- **Files:** `src/Server.cpp`, `src/ConnectionHandler.cpp`, `include/ConnectionHandler.hpp`
- **Tests:** `./tests bug_005` (a stress test; races don't fail every time, so a pass makes a regression unlikely but doesn't prove it's absent)

**Symptom:** With several clients joining, leaving and running commands at once, the server could crash or garble output. Two clients could also claim the same nickname at the same moment.

**Cause:** `/who`, `/rooms` and the nickname check read the shared `connections` list without `connectionsMutex` while other threads were adding or removing entries. Nicknames and rooms were changed without the lock while other threads read them. Several threads could also write to the same socket at once, mixing their messages together.

**Fix:** Every read of the connection list and every change to a nickname or room now happens while holding `connectionsMutex`. Checking a nickname is free and setting it happen under one lock. `ConnectionHandler::sendMessage` has its own lock, so each message is written to the socket in one piece.

## BUG-006: `/quit` was broadcast as a chat message

- **Date fixed:** 2026-10-09
- **Files:** `src/Server.cpp`
- **Tests:** `./tests bug_006`

**Symptom:** When the GUI disconnected, everyone in the room saw `alice: /quit`.

**Cause:** The server had no `/quit` command, so it treated `/quit` as an ordinary chat message.

**Fix:** The server now handles `/quit` by closing that client's connection without broadcasting anything.

## BUG-007: The chat GUI kept showing the previous room's messages

- **Date fixed:** 2026-10-09
- **Files:** `qt_gui/libchat_gui/src/ChatClient.cpp`, `qt_gui/libchat_gui/src/MainWindow.cpp`, plus the new room history in `src/ChatHistory.cpp` and `src/Server.cpp`
- **Tests:** `./tests bug_007` checks that the server only sends a message to its room. `./tests history_` covers the per-room history the GUI loads. The GUI itself isn't covered by `./tests`, so it was checked by hand (see below).

**Symptom:** After switching rooms, the chat still showed every message from the room you'd left, with the new room's messages added underneath, so it looked like messages were reaching the wrong room. Nothing from before you joined a room was shown. The status bar also kept showing the old room's name.

**Cause:** The GUI had one chat view for the whole connection and never cleared it. The server was already sending messages only to the right room, but the GUI had no way to show one room at a time or load what was said earlier. `onRoomChanged` also returned before updating the status bar whenever the room was already in the sidebar.

**Fix:**
- The server saves chat messages per room (SQLite, `src/ChatHistory.cpp`) and answers `/history` with the current room's recent messages (protocol in the README).
- The GUI asks for history when it connects and every time it switches rooms. When the reply starts, it clears the chat and shows only that room's messages, with their original times and day dividers. Messages from the old room that were still arriving are cleared along with it.
- Rooms open with a single click in the sidebar, and the status bar updates on every room change.

**Manual check:** run a server with two rooms of history and a second client chatting in `dev` while you're in `lobby`. Then click between the rooms: each shows only its own messages, and `dev` includes the message sent while you were away.

## BUG-008: The GUI's room list was often missing rooms

- **Date fixed:** 2026-10-09
- **Files:** `qt_gui/libchat_gui/src/ChatClient.cpp`, `qt_gui/libchat_gui/src/ChatClient.h`
- **Tests:** none in `./tests`, because the GUI isn't built into them. Checked by hand: after connecting, the sidebar lists every active room.

**Symptom:** After connecting, the sidebar usually listed only `lobby`, even when other rooms had people in them, so there was nothing to click to switch rooms.

**Cause:** `/who` and `/rooms` replies are plain lists with no end marker, so the GUI collects one at a time. It requested `/rooms` and then `/who` 200 ms later. Starting the `/who` request threw away the `/rooms` reply that was still being collected.

**Fix:** List requests are queued. A second request waits until the first reply has been collected, and the queue is cleared on disconnect.

## BUG-009: Server tests sometimes crashed inside SQLite

- **Date fixed:** 2026-10-09
- **Files:** `test/ServerHarness.cpp`, `test/main.cpp`, `test/Test.hpp`
- **Tests:** every test that uses `Harness::isolated`. This is probabilistic, so it was checked by running the full suite 60 times in a row with no failures (before the fix, about 1 run in 25 of `./tests history_` crashed).

**Symptom:** Now and then, every server test that uses chat history failed in the same run with `test process crashed: Segmentation fault: 11`. Rerunning usually passed.

**Cause:** `Harness::isolated` ran each test in a `fork()`ed copy of the test process without starting a new program. On macOS, system libraries that the parent process has already used aren't safe to use in such a child. The parent had already run the database tests, and SQLite's `open` crashed in the child inside macOS's logging code (`os_signpost`). LibChat itself never forks, so the real server wasn't affected.

**Fix:** `Harness::isolated` starts a fresh copy of the test binary (`./tests --child <test name>`) with `posix_spawn`. That copy runs only that test and reports any failure message back through a pipe. Crashes, timeouts and failure messages are reported the same way as before.

---

## Known issues (not fixed yet)

- A slow or stuck client can delay everyone else, because broadcasts send to each client while holding the lock on the shared client list.
- Logging from different client threads isn't synchronized, so server log lines can run together.

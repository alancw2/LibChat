// Regression tests for the fixes described in BUGFIXES.md.
// Name each test bug_<number>_<what it checks> so `./tests bug_<number>` runs
// just that bug's tests.
#include "Test.hpp"
#include "ServerHarness.hpp"
#include "../include/ConnectionHandler.hpp"
#include "../include/Protocol.hpp"
#include <arpa/inet.h>
#include <chrono>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

using Harness::Client;

// ── BUG-001: peer labels contained NUL bytes and garbage ─────────────

TEST(bug_001_ipv4_peer_address_is_clean) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(50407);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    std::string label = Protocol::formatPeerAddress(reinterpret_cast<sockaddr*>(&addr));
    CHECK(label == "127.0.0.1:50407");
    CHECK(label.find('\0') == std::string::npos);
}

TEST(bug_001_ipv6_peer_address_is_clean) {
    sockaddr_in6 addr{};
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(8080);
    inet_pton(AF_INET6, "::1", &addr.sin6_addr);
    CHECK(Protocol::formatPeerAddress(reinterpret_cast<sockaddr*>(&addr)) == "[::1]:8080");
}

TEST(bug_001_unknown_address_family_gives_empty_label) {
    sockaddr addr{};
    addr.sa_family = AF_UNIX;
    CHECK(Protocol::formatPeerAddress(&addr).empty());
}

TEST(bug_001_two_clients_on_same_ip_get_clean_distinct_labels) {
    Harness::isolated([] {
        int port = Harness::startServer();
        Client a(port), b(port);
        std::string labelA = a.localAddress();
        std::string labelB = b.localAddress();
        CHECK(labelA != labelB);
        // /who lists every label in the room; each must appear exactly, with no junk.
        a.send("/who\n");
        CHECK(a.waitFor(labelA + "\n"));
        CHECK(a.waitFor(labelB + "\n"));
        CHECK(a.received.find('\0') == std::string::npos);

        b.send("hi\n");
        CHECK(a.waitFor(labelB + ": hi\n"));
    });
}

// ── BUG-002: messages weren't split on newlines ──────────────────────

TEST(bug_002_line_buffer_splits_batched_messages) {
    Protocol::LineBuffer lines;
    lines.append("a\nb\n", 4);
    CHECK(lines.nextLine() == "a");
    CHECK(lines.nextLine() == "b");
    CHECK(!lines.nextLine());
}

TEST(bug_002_line_buffer_waits_for_rest_of_split_message) {
    Protocol::LineBuffer lines;
    lines.append("hel", 3);
    CHECK(!lines.nextLine());
    lines.append("lo\nwor", 6);
    CHECK(lines.nextLine() == "hello");
    CHECK(!lines.nextLine());
    CHECK(lines.pending() == 3);
}

TEST(bug_002_line_buffer_strips_crlf) {
    Protocol::LineBuffer lines;
    lines.append("hi\r\n", 4);
    CHECK(lines.nextLine() == "hi");
}

TEST(bug_002_line_buffer_returns_empty_lines) {
    Protocol::LineBuffer lines;
    lines.append("\n", 1);
    CHECK(lines.nextLine() == "");
}

TEST(bug_002_line_buffer_caps_long_lines) {
    Protocol::LineBuffer lines;
    std::string big(Protocol::MAX_LINE_LENGTH + 100, 'x');
    lines.append(big.data(), big.size());
    auto first = lines.nextLine();
    CHECK(first && first->size() == Protocol::MAX_LINE_LENGTH);
    CHECK(lines.pending() == 100);
}

TEST(bug_002_batched_commands_all_run) {
    Harness::isolated([] {
        int port = Harness::startServer();
        Client a(port);
        // The GUI sends these back to back; they often arrive in one recv().
        a.send("/nick alice\n/join dev\n");
        CHECK(a.waitFor("has changed their nickname to alice\n"));
        CHECK(a.waitFor("alice has entered room dev\n"));
    });
}

TEST(bug_002_two_lines_in_one_send_are_two_messages) {
    Harness::isolated([] {
        int port = Harness::startServer();
        Client a(port), b(port);
        CHECK(a.nick("alice"));
        b.send("hello\nworld\n");
        CHECK(a.waitFor(b.localAddress() + ": world\n"));
        CHECK(a.received.find(b.localAddress() + ": hello\n") != std::string::npos);
    });
}

TEST(bug_002_message_split_across_sends_arrives_whole) {
    Harness::isolated([] {
        int port = Harness::startServer();
        Client a(port), b(port);
        CHECK(a.nick("alice"));
        CHECK(b.nick("bob"));
        a.send("hel");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        a.send("lo\n");
        CHECK(b.waitFor("alice: hello\n"));
        CHECK(b.received.find("alice: hel\n") == std::string::npos);
    });
}

TEST(bug_002_join_message_has_no_stray_newline) {
    Harness::isolated([] {
        int port = Harness::startServer();
        Client a(port);
        CHECK(a.nick("alice"));
        a.send("/join dev\r\n");
        CHECK(a.waitFor("alice has entered room dev\n"));
        CHECK(a.received.find("dev\r") == std::string::npos);
        CHECK(a.received.find("dev\n\n") == std::string::npos);
    });
}

// ── BUG-003: server crashed when a client disconnected abruptly ──────

TEST(bug_003_send_to_closed_peer_does_not_raise_sigpipe) {
    Harness::isolated([] {
        int fds[2];
        CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
        ConnectionHandler conn(fds[0], "test");
        close(fds[1]);
        // Without SO_NOSIGPIPE / MSG_NOSIGNAL this raises SIGPIPE and the
        // process dies instead of send() just failing.
        conn.sendMessage("hello\n");
        conn.sendMessage("hello again\n");
        close(fds[0]);
    });
}

TEST(bug_003_server_survives_client_reset) {
    Harness::isolated([] {
        int port = Harness::startServer();
        Client a(port), b(port);
        CHECK(a.nick("alice"));
        CHECK(b.nick("bob"));
        {
            Client c(port);
            CHECK(c.nick("carol"));
            c.resetAbruptly();
        }
        // Broadcasting right after the reset used to raise SIGPIPE or make
        // recv() return -1, which killed the whole server.
        for (int i = 0; i < 20; ++i)
            a.send("ping " + std::to_string(i) + "\n");
        CHECK(b.waitFor("alice: ping 19\n"));

        Client d(port);
        CHECK(d.nick("dave"));
    });
}

TEST(bug_003_server_survives_many_resets) {
    Harness::isolated([] {
        int port = Harness::startServer();
        Client a(port);
        CHECK(a.nick("alice"));
        for (int i = 0; i < 25; ++i) {
            Client c(port);
            c.send("bye\n");
            c.resetAbruptly();
            a.send("still here " + std::to_string(i) + "\n");
        }
        CHECK(a.waitFor("alice: still here 24\n"));
    });
}

// ── BUG-004: invalid nicknames were announced as changed ─────────────

TEST(bug_004_invalid_nickname_is_rejected) {
    Harness::isolated([] {
        int port = Harness::startServer();
        Client a(port), b(port);
        CHECK(b.nick("bob"));
        a.send("/nick bad!!name\n");
        CHECK(a.waitFor("Nickname change failed: invalid nickname.\n"));
        b.drain();
        CHECK(b.received.find("bad!!name") == std::string::npos);

        a.send("hi\n");
        CHECK(b.waitFor(a.localAddress() + ": hi\n"));
    });
}

TEST(bug_004_taken_nickname_reply_starts_with_Nickname) {
    Harness::isolated([] {
        int port = Harness::startServer();
        Client a(port), b(port);
        CHECK(a.nick("alice"));
        b.send("/nick alice\n");
        // The GUI only rolls back a nickname on lines starting with "Nickname".
        CHECK(b.waitFor("\nNickname is taken.\n") || b.received.starts_with("Nickname is taken.\n"));
    });
}

// ── BUG-005: shared connection list was used without locking ─────────

TEST(bug_005_concurrent_clients_do_not_crash_server) {
    Harness::isolated([] {
        int port = Harness::startServer();
        std::vector<std::thread> workers;
        for (int t = 0; t < 8; ++t) {
            workers.emplace_back([port, t] {
                for (int i = 0; i < 20; ++i) {
                    Client c(port);
                    c.send("/nick u" + std::to_string(t) + "_" + std::to_string(i) +
                           "\n/who\n/rooms\n/join r" + std::to_string(i % 3) + "\nhello\n");
                    if (i % 2)
                        c.resetAbruptly();
                }
            });
        }
        for (auto& w : workers)
            w.join();

        Client check(port);
        CHECK(check.nick("survivor"));
    }, 60);
}

// ── BUG-006: /quit was broadcast as a chat message ───────────────────

TEST(bug_006_quit_closes_connection_without_broadcast) {
    Harness::isolated([] {
        int port = Harness::startServer();
        Client a(port), b(port);
        CHECK(a.nick("alice"));
        CHECK(b.nick("bob"));
        a.send("/quit\n");
        CHECK(a.waitForClose());
        b.drain();
        CHECK(b.received.find("/quit") == std::string::npos);
    });
}

// ── BUG-007: GUI showed other rooms' messages ────────────────────────
// The GUI fix itself is checked by hand (see BUGFIXES.md); this covers the
// server guarantee it relies on. history_* tests in HistoryTest.cpp cover the
// per-room history the GUI now loads.

TEST(bug_007_room_messages_only_reach_clients_in_that_room) {
    Harness::isolated([] {
        int port = Harness::startServer();
        Client alice(port), bob(port);
        CHECK(alice.nick("alice"));
        CHECK(bob.nick("bob"));
        alice.send("/join dev\n");
        CHECK(alice.waitFor("alice has entered room dev\n"));

        bob.send("lobby only\n");
        alice.send("dev only\n");
        CHECK(bob.waitFor("bob: lobby only\n"));
        CHECK(alice.waitFor("alice: dev only\n"));
        alice.drain();
        bob.drain();
        CHECK(alice.received.find("lobby only") == std::string::npos);
        CHECK(bob.received.find("dev only") == std::string::npos);
    });
}

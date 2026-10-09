// Tests for runServer's ServerOptions (used by the debug GUI).
#include "Test.hpp"
#include "ServerHarness.hpp"
#include "../include/Server.hpp"
#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <thread>
#include <unistd.h>
#include <vector>

using Harness::Client;

TEST(server_stop_returns_and_disconnects_clients) {
    Harness::isolated([] {
        int port = 0;
        int fd = Harness::listenOnLoopback(&port);
        std::atomic<bool> stop{false};
        ServerOptions options;
        options.stop = &stop;
        options.log = [](const std::string&) {};
        auto finished = std::async(std::launch::async, [&] { runServer(fd, options); });

        Client a(port);
        CHECK(a.nick("alice"));
        stop = true;
        CHECK(finished.wait_for(std::chrono::seconds(3)) == std::future_status::ready);
        CHECK(a.waitForClose());
        close(fd);
    });
}

TEST(server_log_reports_connects_lines_and_disconnects) {
    Harness::isolated([] {
        int port = 0;
        int fd = Harness::listenOnLoopback(&port);
        std::mutex logMutex;
        std::vector<std::string> events;
        ServerOptions options;
        options.log = [&](const std::string& e) {
            std::scoped_lock lock(logMutex);
            events.push_back(e);
        };
        std::thread([fd, options] { runServer(fd, options); }).detach();

        auto logged = [&](const std::string& needle) {
            for (int i = 0; i < 100; ++i) {
                {
                    std::scoped_lock lock(logMutex);
                    for (const auto& e : events)
                        if (e.find(needle) != std::string::npos)
                            return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            return false;
        };

        {
            Client a(port);
            CHECK(a.nick("alice"));
            a.send("hello there\n");
            CHECK(logged("connected from 127.0.0.1:"));
            CHECK(logged("(alice) > hello there"));
        }
        CHECK(logged("disconnected"));
    });
}

#include <arpa/inet.h>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace ftxui;

namespace {

struct ChatMessage {
    std::string text;
    bool is_status = false;
};

enum class FocusTarget {
    Transcript,
    Input,
};

constexpr int kPageScrollSize = 6;

std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t start = 0;

    while (start <= text.size()) {
        const std::size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            lines.push_back(text.substr(start));
            break;
        }

        lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }

    if (lines.empty()) {
        lines.push_back("");
    }

    return lines;
}

int CountRenderedLines(const std::vector<ChatMessage>& messages) {
    if (messages.empty()) {
        return 1;
    }

    int total_lines = 0;
    for (std::size_t i = 0; i < messages.size(); ++i) {
        total_lines += static_cast<int>(SplitLines(messages[i].text).size());
        if (i + 1 != messages.size()) {
            total_lines += 1;
        }
    }

    return std::max(total_lines, 1);
}

int ClampScrollLine(int line, int total_lines) {
    if (total_lines <= 0) {
        return 0;
    }

    return std::clamp(line, 0, total_lines - 1);
}

void MoveScrollToLatest(int& scroll_index, bool& follow_latest, int total_lines) {
    follow_latest = true;
    scroll_index = ClampScrollLine(total_lines - 1, total_lines);
}

void MoveScrollBy(int delta, int& scroll_index, bool& follow_latest, int total_lines) {
    if (total_lines <= 0) {
        scroll_index = 0;
        follow_latest = true;
        return;
    }

    follow_latest = false;
    scroll_index = ClampScrollLine(scroll_index + delta, total_lines);

    if (scroll_index >= total_lines - 1) {
        MoveScrollToLatest(scroll_index, follow_latest, total_lines);
    }
}

void AppendMessage(std::vector<ChatMessage>& messages,
                   const ChatMessage& message,
                   int& scroll_index,
                   bool& follow_latest) {
    messages.push_back(message);
    const int total_lines = CountRenderedLines(messages);

    if (follow_latest) {
        MoveScrollToLatest(scroll_index, follow_latest, total_lines);
        return;
    }

    scroll_index = ClampScrollLine(scroll_index, total_lines);
}

Element RenderMessage(const ChatMessage& message) {
    Elements lines;

    for (const std::string& line : SplitLines(message.text)) {
        lines.push_back(paragraph(line.empty() ? " " : line));
    }

    Element block = vbox(std::move(lines));
    block |= color(message.is_status ? Color::GrayLight : Color::White);
    return block;
}

}  // namespace

int main() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("Socket creation failed");
        return 1;
    }

    std::string server_ip;
    std::cout << "Enter the IP address of the chat-server: ";
    std::cin >> server_ip;

    sockaddr_in server_address{};
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(8080);

    if (inet_pton(AF_INET, server_ip.c_str(), &server_address.sin_addr) != 1) {
        std::cerr << "Invalid IP address.\n";
        close(sock);
        return 1;
    }

    if (connect(sock, reinterpret_cast<sockaddr*>(&server_address), sizeof(server_address)) == -1) {
        perror("Connect failed");
        close(sock);
        return 1;
    }

    auto screen = ScreenInteractive::Fullscreen();

    std::vector<ChatMessage> messages{
        {"--- Connected to Chat Server ---", true},
        {"Commands: /help, /nick <name>, /join <room>, /rooms, /quit", true},
    };
    std::mutex messages_mutex;

    std::string input;
    int scroll_index = static_cast<int>(messages.size()) - 1;
    bool follow_latest = true;
    std::atomic<bool> running{true};
    std::atomic<bool> connected{true};
    Box transcript_box;
    FocusTarget focus_target = FocusTarget::Input;

    auto input_box = Input(&input, connected ? "Type a message..." : "Disconnected");
    input_box->TakeFocus();

    auto layout = Container::Vertical({
        input_box,
    });

    auto renderer = Renderer(layout, [&] {
        std::vector<ChatMessage> snapshot;
        int scroll_line = 0;
        bool scroll_to_latest = false;

        {
            std::lock_guard<std::mutex> lock(messages_mutex);
            snapshot = messages;
            scroll_line = scroll_index;
            scroll_to_latest = follow_latest;
        }

        Elements transcript;
        transcript.reserve(snapshot.empty() ? 1 : snapshot.size() * 2);

        if (snapshot.empty()) {
            transcript.push_back(text("No messages yet.") | dim);
        } else {
            for (std::size_t i = 0; i < snapshot.size(); ++i) {
                transcript.push_back(RenderMessage(snapshot[i]));

                if (i + 1 != snapshot.size()) {
                    transcript.push_back(separatorEmpty());
                }
            }
        }

        const int total_lines = CountRenderedLines(snapshot);
        const int target_line = scroll_to_latest ? total_lines - 1 : ClampScrollLine(scroll_line, total_lines);
        Element transcript_document = vbox(std::move(transcript)) | focusPosition(0, target_line);
        const bool transcript_focused = focus_target == FocusTarget::Transcript;
        const bool input_focused = focus_target == FocusTarget::Input;

        const std::string status = connected ? "Connected" : "Disconnected";
        const Color status_color = connected ? Color::GreenLight : Color::RedLight;

        return vbox({
                   text("LibChat Client") | bold | center,
                   text(status) | color(status_color) | center,
                   separator(),
                   window(
                       hbox({
                           text(" Chat ") | bold,
                           filler(),
                           text(transcript_focused ? "NAV" : "VIEW") | dim,
                       }),
                       transcript_document | vscroll_indicator | yframe | reflect(transcript_box) | flex
                   ) | color(transcript_focused ? Color::CyanLight : Color::White) | flex,
                   separator(),
                   hbox({
                       text("> ") | color(Color::CyanLight),
                       input_box->Render() | flex,
                   }) | color(input_focused ? Color::CyanLight : Color::White) | border,
                   text(input_focused
                            ? "Tab switches to transcript navigation. Enter sends."
                            : "Tab switches back to input. Up/Down, PgUp/PgDn, Home/End, and wheel scroll the chat.")
                       | dim,
               }) |
               border;
    });

    auto component = CatchEvent(renderer, [&](Event event) {
        const auto scroll = [&](int delta) {
            std::lock_guard<std::mutex> lock(messages_mutex);
            MoveScrollBy(delta, scroll_index, follow_latest, CountRenderedLines(messages));
            return true;
        };

        if (event == Event::Tab || event == Event::TabReverse) {
            focus_target = focus_target == FocusTarget::Input ? FocusTarget::Transcript : FocusTarget::Input;
            if (focus_target == FocusTarget::Input) {
                input_box->TakeFocus();
            }
            return true;
        }

        if (event.is_mouse() && transcript_box.Contain(event.mouse().x, event.mouse().y)) {
            focus_target = FocusTarget::Transcript;
        }

        if (focus_target == FocusTarget::Transcript && event == Event::PageUp) {
            const int page_size = std::max(transcript_box.y_max - transcript_box.y_min, kPageScrollSize);
            return scroll(-page_size);
        }

        if (focus_target == FocusTarget::Transcript && event == Event::PageDown) {
            const int page_size = std::max(transcript_box.y_max - transcript_box.y_min, kPageScrollSize);
            return scroll(page_size);
        }

        if (focus_target == FocusTarget::Transcript && event == Event::ArrowUpCtrl) {
            return scroll(-1);
        }

        if (focus_target == FocusTarget::Transcript && event == Event::ArrowDownCtrl) {
            return scroll(1);
        }

        if (focus_target == FocusTarget::Transcript && event == Event::ArrowUp) {
            return scroll(-1);
        }

        if (focus_target == FocusTarget::Transcript && event == Event::ArrowDown) {
            return scroll(1);
        }

        if (focus_target == FocusTarget::Transcript && event == Event::Home) {
            std::lock_guard<std::mutex> lock(messages_mutex);
            follow_latest = false;
            scroll_index = 0;
            return true;
        }

        if (focus_target == FocusTarget::Transcript && event == Event::End) {
            std::lock_guard<std::mutex> lock(messages_mutex);
            MoveScrollToLatest(scroll_index, follow_latest, CountRenderedLines(messages));
            return true;
        }

        if (event.is_mouse()) {
            if (event.mouse().button == Mouse::WheelUp) {
                focus_target = FocusTarget::Transcript;
                return scroll(-1);
            }
            if (event.mouse().button == Mouse::WheelDown) {
                focus_target = FocusTarget::Transcript;
                return scroll(1);
            }
        }

        if (focus_target == FocusTarget::Input && event == Event::Return) {
            if (input == "/quit") {
                running = false;
                shutdown(sock, SHUT_RDWR);
                screen.ExitLoopClosure()();
                return true;
            }

            if (input.empty()) {
                return true;
            }

            if (!connected) {
                std::lock_guard<std::mutex> lock(messages_mutex);
                AppendMessage(messages, {"[Disconnected: unable to send message]", true}, scroll_index, follow_latest);
                screen.PostEvent(Event::Custom);
                return true;
            }

            if (send(sock, input.c_str(), input.size(), 0) == -1) {
                {
                    std::lock_guard<std::mutex> lock(messages_mutex);
                    AppendMessage(messages, {"[Failed to send message]", true}, scroll_index, follow_latest);
                }
                connected = false;
                running = false;
                screen.PostEvent(Event::Custom);
                shutdown(sock, SHUT_RDWR);
                screen.ExitLoopClosure()();
                return true;
            }

            input.clear();
            screen.PostEvent(Event::Custom);
            return true;
        }

        return false;
    });

    std::thread receive_thread([&] {
        char buffer[1024];

        while (running) {
            std::memset(buffer, 0, sizeof(buffer));
            const int bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);

            if (bytes > 0) {
                {
                    std::lock_guard<std::mutex> lock(messages_mutex);
                    AppendMessage(messages, {std::string(buffer, bytes), false}, scroll_index, follow_latest);
                }
                screen.PostEvent(Event::Custom);
                continue;
            }

            connected = false;

            {
                std::lock_guard<std::mutex> lock(messages_mutex);
                if (bytes == 0) {
                    AppendMessage(messages, {"[Server closed connection]", true}, scroll_index, follow_latest);
                } else if (running) {
                    AppendMessage(messages, {"[Receive error / disconnected]", true}, scroll_index, follow_latest);
                }
            }

            screen.PostEvent(Event::Custom);
            break;
        }
    });

    screen.Loop(component);

    running = false;
    shutdown(sock, SHUT_RDWR);
    close(sock);

    if (receive_thread.joinable()) {
        receive_thread.join();
    }

    return 0;
}

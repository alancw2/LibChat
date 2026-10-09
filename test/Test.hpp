// Minimal test framework shared by every test file.
//
//   TEST(my_test_name) {
//       CHECK(1 + 1 == 2);
//   }
//
// Tests register themselves, so a new file only needs to be added to
// TEST_SRCS in the Makefile.
#ifndef LIBCHAT_TEST_HPP
#define LIBCHAT_TEST_HPP

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace Test {

struct Case {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

// Set by main(): the test binary's path, the test now running, and whether
// this process is a child started to run one isolated test.
inline std::string executablePath;
inline std::string currentTest;
inline bool isChild = false;

// File descriptor a child process writes its failure message to.
inline constexpr int CHILD_RESULT_FD = 3;

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) {
        registry().push_back({name, std::move(fn)});
    }
};

}

#define TEST(name)                                                    \
    static void test_##name();                                        \
    static Test::Registrar registrar_##name(#name, test_##name);      \
    static void test_##name()

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            throw std::runtime_error(std::string("CHECK failed: " #cond " (") + \
                                     __FILE__ + ":" + std::to_string(__LINE__) + ")"); \
        }                                                                        \
    } while (0)

#endif

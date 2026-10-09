// Runs every registered test. Build and run with: make tests
// Pass a substring to run only matching tests, e.g. ./tests bug_001
//
// Harness::isolated() runs a test in a fresh copy of this program, started
// as: ./tests --child <exact test name>
#include "Test.hpp"
#include <cstring>
#include <unistd.h>

namespace {
// Runs exactly one test inside a child process and reports a failure message
// on CHILD_RESULT_FD.
int runChild(const std::string& name) {
    Test::isChild = true;
    for (const auto& test : Test::registry()) {
        if (test.name != name)
            continue;
        Test::currentTest = test.name;
        std::string error;
        try {
            test.fn();
        } catch (const std::exception& e) {
            error = e.what();
        } catch (...) {
            error = "unknown exception";
        }
        if (!error.empty())
            write(Test::CHILD_RESULT_FD, error.data(), error.size());
        _exit(error.empty() ? 0 : 1);
    }
    std::string error = "no test named " + name;
    write(Test::CHILD_RESULT_FD, error.data(), error.size());
    _exit(1);
}
}

int main(int argc, char** argv) {
    Test::executablePath = argv[0];
    if (argc == 3 && std::strcmp(argv[1], "--child") == 0)
        return runChild(argv[2]);

    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0;
    int failures = 0;

    for (const auto& test : Test::registry()) {
        if (filter && test.name.find(filter) == std::string::npos)
            continue;
        ++run;
        Test::currentTest = test.name;
        try {
            test.fn();
            std::cout << "[PASS] " << test.name << "\n";
        } catch (const std::exception& e) {
            ++failures;
            std::cout << "[FAIL] " << test.name << ": " << e.what() << "\n";
        }
    }

    std::cout << "\n" << (run - failures) << "/" << run << " tests passed\n";
    return failures == 0 && run > 0 ? 0 : 1;
}

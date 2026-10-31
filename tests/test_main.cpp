#include "TestHarness.h"

#include <cstring>

// Usage: wb_tests [substring]   - runs every test whose name contains it.
int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0, failedTests = 0;

    for (const auto& t : wbtest::registry()) {
        if (filter && std::strstr(t.name, filter) == nullptr) continue;
        const int before = wbtest::failureCount();
        try {
            t.fn();
        } catch (const std::exception& e) {
            wbtest::reportFailure(__FILE__, __LINE__, std::string("unexpected exception: ") + e.what());
        }
        const bool ok = wbtest::failureCount() == before;
        std::cout << (ok ? "[ OK ] " : "[FAIL] ") << t.name << '\n';
        ++run;
        if (!ok) ++failedTests;
    }

    std::cout << "\n" << run - failedTests << "/" << run << " tests passed\n";
    return (failedTests == 0 && run > 0) ? 0 : 1;
}

// Minimal test harness - enough for this project without pulling in a
// framework. TEST() registers a function; CHECK* record failures and keep
// going so one run shows every broken assertion.
#pragma once

#include <cmath>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace wbtest {

struct TestCase {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failureCount() {
    static int n = 0;
    return n;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

inline void reportFailure(const char* file, int line, const std::string& what) {
    ++failureCount();
    std::cerr << "    FAIL " << file << ':' << line << "  " << what << '\n';
}

}  // namespace wbtest

#define WB_CONCAT_(a, b) a##b
#define WB_CONCAT(a, b) WB_CONCAT_(a, b)

#define TEST(name)                                                                   \
    static void name();                                                              \
    static const wbtest::Registrar WB_CONCAT(registrar_, name)(#name, name);         \
    static void name()

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) wbtest::reportFailure(__FILE__, __LINE__, "CHECK(" #cond ")");  \
    } while (0)

#define CHECK_NEAR(actual, expected, tol)                                            \
    do {                                                                             \
        const double a_ = (actual), e_ = (expected), t_ = (tol);                     \
        if (!(std::abs(a_ - e_) <= t_)) {                                            \
            wbtest::reportFailure(__FILE__, __LINE__,                                \
                std::string(#actual) + " = " + std::to_string(a_) + ", expected " +  \
                std::to_string(e_) + " +/- " + std::to_string(t_));                  \
        }                                                                            \
    } while (0)

// Passes only if `expr` throws wb::WorkbenchError with the given code.
#define CHECK_THROWS_CODE(expr, expectedCode)                                        \
    do {                                                                             \
        bool threw_ = false;                                                         \
        try { (void)(expr); } catch (const wb::WorkbenchError& e_) {                 \
            threw_ = true;                                                           \
            if (e_.code() != (expectedCode))                                         \
                wbtest::reportFailure(__FILE__, __LINE__,                            \
                    std::string("wrong error code: ") + wb::toString(e_.code()) +    \
                    " (" + e_.what() + ")");                                         \
        } catch (...) {                                                              \
            threw_ = true;                                                           \
            wbtest::reportFailure(__FILE__, __LINE__, "threw a non-WorkbenchError"); \
        }                                                                            \
        if (!threw_) wbtest::reportFailure(__FILE__, __LINE__, #expr " did not throw"); \
    } while (0)

// Tiny self-contained test harness so the project has no external test dependency.
#pragma once

#include <cmath>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace sptest {

struct TestCase {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) {
        registry().push_back({name, std::move(fn)});
    }
};

struct Failure {
    std::string message;
};

}  // namespace sptest

#define SP_CONCAT_INNER(a, b) a##b
#define SP_CONCAT(a, b) SP_CONCAT_INNER(a, b)

#define TEST(name)                                                                  \
    static void name();                                                             \
    static ::sptest::Registrar SP_CONCAT(registrar_, name)(#name, &name);           \
    static void name()

#define SP_FAIL(msg)                                                                \
    do {                                                                            \
        std::ostringstream sp_oss_;                                                 \
        sp_oss_ << __FILE__ << ":" << __LINE__ << ": " << msg;                      \
        throw ::sptest::Failure{sp_oss_.str()};                                     \
    } while (0)

#define EXPECT_TRUE(cond)                                                           \
    do {                                                                            \
        if (!(cond)) SP_FAIL("expected true: " #cond);                              \
    } while (0)

#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))

#define EXPECT_EQ(a, b)                                                             \
    do {                                                                            \
        const auto& sp_a_ = (a);                                                    \
        const auto& sp_b_ = (b);                                                    \
        if (!(sp_a_ == sp_b_))                                                      \
            SP_FAIL(#a " == " #b " (" << sp_a_ << " vs " << sp_b_ << ")");          \
    } while (0)

#define EXPECT_NEAR(a, b, eps)                                                      \
    do {                                                                            \
        const double sp_a_ = (a);                                                   \
        const double sp_b_ = (b);                                                   \
        if (std::fabs(sp_a_ - sp_b_) > (eps))                                       \
            SP_FAIL(#a " ~= " #b " (" << sp_a_ << " vs " << sp_b_ << ")");          \
    } while (0)

#pragma once
// Minimal header-only test harness: TEST(name) registers, CHECK/CHECK_NEAR assert.
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

namespace sns_test {

struct Case {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<Case>& Registry() {
    static std::vector<Case> cases;
    return cases;
}

inline int& Failures() {
    static int failures = 0;
    return failures;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { Registry().push_back({name, std::move(fn)}); }
};

}  // namespace sns_test

#define TEST(name)                                                           \
    static void name();                                                      \
    static sns_test::Registrar registrar_##name(#name, name);                \
    static void name()

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            std::printf("    CHECK failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
            ++sns_test::Failures();                                                   \
        }                                                                             \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                               \
    do {                                                                                    \
        const double va_ = (a), vb_ = (b);                                                  \
        if (!(std::fabs(va_ - vb_) <= (tol))) {                                             \
            std::printf("    CHECK_NEAR failed: %s=%g vs %s=%g tol=%g (%s:%d)\n", #a, va_, #b, vb_, \
                        static_cast<double>(tol), __FILE__, __LINE__);                      \
            ++sns_test::Failures();                                                         \
        }                                                                                   \
    } while (0)

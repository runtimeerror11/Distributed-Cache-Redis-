#pragma once
// Minimal test runner: TEST(name) { CHECK(...); CHECK_EQ(a, b); }
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace check {

struct Case {
    const char *name;
    std::function<void()> fn;
};

inline std::vector<Case> &registry() {
    static std::vector<Case> cases;
    return cases;
}

inline int &failures() {
    static int n = 0;
    return n;
}

struct Register {
    Register(const char *name, std::function<void()> fn) { registry().push_back({name, fn}); }
};

template <typename T>
std::string show(const T &v) {
    std::ostringstream os;
    os << v;
    return os.str();
}

}  // namespace check

#define CHECK_CAT2(a, b) a##b
#define CHECK_CAT(a, b) CHECK_CAT2(a, b)

#define TEST(name)                                                            \
    static void name();                                                       \
    static check::Register CHECK_CAT(reg_, name)(#name, name);                \
    static void name()

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            std::fprintf(stderr, "  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            check::failures()++;                                                      \
        }                                                                             \
    } while (0)

#define CHECK_EQ(a, b)                                                                     \
    do {                                                                                   \
        auto va_ = (a);                                                                    \
        auto vb_ = (b);                                                                    \
        if (!(va_ == vb_)) {                                                               \
            std::fprintf(stderr, "  FAILED %s:%d: %s == %s (%s vs %s)\n", __FILE__, __LINE__, \
                         #a, #b, check::show(va_).c_str(), check::show(vb_).c_str());      \
            check::failures()++;                                                           \
        }                                                                                  \
    } while (0)

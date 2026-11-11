#include <cstring>

#include "check.h"

// Runs every registered test, or only those whose name contains argv[1].
int main(int argc, char **argv) {
    int ran = 0;
    for (const check::Case &c : check::registry()) {
        if (argc > 1 && !std::strstr(c.name, argv[1])) continue;
        int before = check::failures();
        c.fn();
        std::fprintf(stderr, "%s %s\n", check::failures() == before ? "[ ok ]" : "[FAIL]", c.name);
        ran++;
    }
    std::fprintf(stderr, "%d tests, %d failed checks\n", ran, check::failures());
    return check::failures() == 0 ? 0 : 1;
}

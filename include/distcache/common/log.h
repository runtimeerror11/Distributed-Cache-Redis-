#pragma once
#include <cstdarg>
#include <cstdio>

namespace distcache {

// Minimal logging to stderr, one line per call. printf-style, checked by the compiler.
__attribute__((format(printf, 1, 2))) inline void log_line(const char *fmt, ...) {
    std::fputs("[distcache] ", stderr);
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
    std::fputc('\n', stderr);
}

}  // namespace distcache

#define DC_LOG distcache::log_line

#ifndef EGTRAIN_PORTABILITY_H
#define EGTRAIN_PORTABILITY_H

// Cross-platform portability shims for EGTRAIN.
// MSVC-specific functions are replaced with C99/C++17 standard equivalents.

#include <cstdio>
#include <cstring>
#include <sys/stat.h>

#if !defined(_MSC_VER)
// MSVC _s functions are not standard C/C++. Map them to safe equivalents.

// sprintf_s(buf, ...) -> snprintf(buf, sizeof(buf), ...)
// Note: sizeof(buf) is correct for stack-allocated arrays, less so for pointers.
#define sprintf_s(buf, ...) snprintf(buf, sizeof(buf), __VA_ARGS__)

// strcpy_s(dst, src) -> strcpy(dst, src)
#define strcpy_s(dst, src) strcpy(dst, src)

// strcpy_s(dst, len, src) -> strncpy(dst, src, len) (3-arg variant, rare)
// Not needed for this codebase; all calls are 2-arg.

// _mkdir(path) -> mkdir(path, 0777) (POSIX needs mode argument)
#define _mkdir(path) mkdir(path, 0777)

#endif

#endif // EGTRAIN_PORTABILITY_H

#ifndef EGTRAIN_PORTABILITY_H
#define EGTRAIN_PORTABILITY_H

// Cross-platform portability shims for EGTRAIN.
// The codebase mixes MSVC names (_s functions, _mkdir) and POSIX names
// (localtime_r, unistd.h). This header maps whichever set is missing on the
// current compiler onto the one that is available.

#include <cstdio>
#include <cstring>
#include <sys/stat.h>

#if defined(_MSC_VER)

#include <io.h>      // _isatty
#include <direct.h>  // _mkdir
#include <ctime>     // localtime_s

// POSIX terminal check -> MSVC equivalents.
#define isatty _isatty
#define STDIN_FILENO _fileno(stdin)

// POSIX localtime_r -> MSVC localtime_s (arguments swapped, errno_t return).
static inline struct tm* localtime_r(const time_t* timep, struct tm* result) {
	return localtime_s(result, timep) == 0 ? result : nullptr;
}

#else

#include <unistd.h>  // isatty, STDIN_FILENO

// MSVC _s functions are not standard C/C++. Map them to safe equivalents.
// sprintf_s(buf, ...) -> snprintf(buf, sizeof(buf), ...)
// Note: sizeof(buf) is correct for stack-allocated arrays, less so for pointers.
#define sprintf_s(buf, ...) snprintf(buf, sizeof(buf), __VA_ARGS__)

// strcpy_s(dst, src) -> strcpy(dst, src)
#define strcpy_s(dst, src) strcpy(dst, src)

// _mkdir(path) -> mkdir(path, 0777) (POSIX needs a mode argument)
#define _mkdir(path) mkdir(path, 0777)

#endif

#endif // EGTRAIN_PORTABILITY_H

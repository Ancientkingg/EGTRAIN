#ifndef EGTRAIN_QT_MSVC_COMPAT_H
#define EGTRAIN_QT_MSVC_COMPAT_H

// Forced include for MSVC builds (see the root CMakeLists.txt).
//
// QList and QVector in Qt 5.15 pass stdext::make_checked_array_iterator to
// standard algorithms on MSVC. The standard library of Visual Studio 2026 no
// longer has it. With every other compiler Qt passes the plain pointer, and
// the macros below select that for MSVC as well.

#if defined(_MSC_VER) && defined(__cplusplus)
#if __has_include(<QtCore/qglobal.h>)
#include <QtCore/qglobal.h>
#undef QT_MAKE_CHECKED_ARRAY_ITERATOR
#undef QT_MAKE_UNCHECKED_ARRAY_ITERATOR
#define QT_MAKE_CHECKED_ARRAY_ITERATOR(x, N) (x)
#define QT_MAKE_UNCHECKED_ARRAY_ITERATOR(x) (x)
#endif
#endif

#endif

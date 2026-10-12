#ifndef P3_TEST_COMPAT_H
#define P3_TEST_COMPAT_H

#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#endif

#endif
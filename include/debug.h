#if !defined(NDEBUG) && !defined(__COSMOCC__) && __has_include("elf.h")
#define __linux__
#define UPRINTF_IMPLEMENTATION
#include "uprintf.h"
#else
#define uprintf(fmt, ...) (void)0;
#endif

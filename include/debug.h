#if !defined(NDEBUG) && __has_include("elf.h")
#define __linux__
#define UPRINTF_IMPLEMENTATION
#include "uprintf.h"
#elif !defined(__COSMOCC__)
#define uprintf(fmt, ...) (void)0;
#endif

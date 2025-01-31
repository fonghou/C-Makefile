#if defined(LOGGING) && __has_include("elf.h") && !defined(__COSMOCC__)
#define __linux__
#define UPRINTF_IMPLEMENTATION
#include "uprintf.h"
#else
#define uprintf(fmt, ...) (void)0;
#endif

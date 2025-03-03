#ifndef DEBUG_H_
#define DEBUG_H_

#ifdef LOGGING
#define ALOG(a)                                                                                    \
  printf("%s:%d %s: " #a " = { beg=%p end=%p used=%td free=%td }\n", __FILE__, __LINE__, __func__, \
         ((a)->beg), ((a)->end), (isize)((a)->end - ((a)->init)), (isize)((a)->end - ((a)->beg)))
#else
#define ALOG(a) ((void)a)
#endif

#if defined(LOGGING) && !defined(__COSMOCC__) && __has_include("elf.h")
#ifndef UPRINTF_IMPLEMENTATION
#include "uprintf.h"
#endif  // UPRINTF_IMPLEMENTATION
#define ULOG(p)                                                   \
  do {                                                            \
    printf("%s:%d %s:\n%s = ", __FILE__, __LINE__, __func__, #p); \
    uprintf("%S\n", p);                                           \
  } while (0)
#else  // LOGGING
#define ULOG(p) ((void)p)
#endif  // LOGGING

#endif  // DEBUG_H_

#ifndef DEBUG_H_
#define DEBUG_H_

#ifdef LOGGING
#define ALOG(a)                                                                             \
  printf("%s:%d %s: " #a " = { beg=%ld end=%ld size=%ld }\n", __FILE__, __LINE__, __func__, \
         (uintptr_t)((a).beg), (uintptr_t)((a).end), (isize)((a).end - ((a).beg)))
#else
#define ALOG(a) ((void)a)
#endif

#if defined(LOGGING) && !defined(__COSMOCC__) && __has_include("elf.h")
#ifndef UPRINTF_IMPLEMENTATION
#include "uprintf.h"
#endif
#define ULOG(p)                                                   \
  do {                                                            \
    printf("%s:%d %s:\n%s = ", __FILE__, __LINE__, __func__, #p); \
    uprintf("%S\n", p);                                           \
  } while (0)
#else
#define ULOG(p) ((void)p)
#endif

#endif  // DEBUG_H_

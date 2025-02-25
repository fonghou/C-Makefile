#ifndef DEBUG_H_
#define DEBUG_H_

#undef assert
#if defined(__clang__)
#define assert(c)                                                                                       \
  do {                                                                                                  \
    if (!(c)) {                                                                                         \
      fprintf(stderr, "Assertion failed: %s in function %s %s:%d\n", #c, __func__, __FILE__, __LINE__); \
      __builtin_debugtrap();                                                                            \
    }                                                                                                   \
  } while (0)
#elif defined(__x86_64__)
#define assert(c)                                                                                       \
  do {                                                                                                  \
    if (!(c)) {                                                                                         \
      fprintf(stderr, "Assertion failed: %s in function %s %s:%d\n", #c, __func__, __FILE__, __LINE__); \
      __asm__ volatile("int3; nop");                                                                    \
    }                                                                                                   \
  } while (0)
#elif defined(__GNUC__)
#define assert(c)                                                                                       \
  do {                                                                                                  \
    if (!(c)) {                                                                                         \
      fprintf(stderr, "Assertion failed: %s in function %s %s:%d\n", #c, __func__, __FILE__, __LINE__); \
      __builtin_trap();                                                                                 \
    }                                                                                                   \
  } while (0)
#elif defined(_MSC_VER)
#define assert(c)                                                                                       \
  do {                                                                                                  \
    if (!(c)) {                                                                                         \
      fprintf(stderr, "Assertion failed: %s in function %s %s:%d\n", #c, __func__, __FILE__, __LINE__); \
      __debugbreak();                                                                                   \
    }                                                                                                   \
  } while (0)
#else
#include <signal.h>
#define assert(c)                                                                                       \
  do {                                                                                                  \
    if (!(c)) {                                                                                         \
      fprintf(stderr, "Assertion failed: %s in function %s %s:%d\n", #c, __func__, __FILE__, __LINE__); \
      raise(SIGTRAP);                                                                                   \
    }                                                                                                   \
  } while (0)
#endif

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

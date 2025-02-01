// clang-format off

#include <stdio.h>

#if defined(__GNUC__) || defined(__clang__)
#define assert(c)                                                              \
  do {                                                                         \
    if (!(c)) {                                                                \
      fprintf(stderr, "Assertion failed: %s at %s:%d\n", #c,                   \
              __FILE__, __LINE__);                                             \
      __builtin_trap();                                                        \
    }                                                                          \
  } while (0)
#elif defined(_MSC_VER)
#define assert(c)                                                              \
  do {                                                                         \
    if (!(c)) {                                                                \
      fprintf(stderr, "Assertion failed: %s at %s:%d\n", #c,                   \
              __FILE__, __LINE__);                                             \
      __debugbreak();                                                          \
    }                                                                          \
  } while (0)
#elif defined(__x86_64__)
#define assert(c)                                                              \
  do {                                                                         \
    if (!(c)) {                                                                \
      fprintf(stderr, "Assertion failed: %s at %s:%d\n", #c,                   \
              __FILE__, __LINE__);                                             \
      __asm__ volatile("int3; nop");                                           \
    }                                                                          \
  } while (0)
#else
#include <assert.h>
#endif

#if defined(LOGGING) && __has_include("elf.h") && !defined(__COSMOCC__)
#define __linux__
#define UPRINTF_IMPLEMENTATION
#include "uprintf.h"
#else
#define uprintf(fmt, ...) (void)0;
#endif

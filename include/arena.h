#ifndef ARENA_H
#define ARENA_H

/** Credit:
    https://nullprogram.com/blog/2023/09/27/
    https://nullprogram.com/blog/2023/10/05/
    https://www.chiark.greenend.org.uk/~sgtatham/quasiblog/c11-generic/#inline
*/

#include <memory.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ALIGN _Alignof(max_align_t)

#if __has_include("debug.h")
#include "debug.h"
#else
#include <assert.h>
#endif

#ifdef __GNUC__
static void autofree_impl(void *p) {
  free(*((void **)p));
}
#define autofree __attribute__((__cleanup__(autofree_impl)))
#else
#warning "autofree is not supported"
#define autofree
#endif

#if defined(__GNUC__) && !defined(__APPLE__)
#undef setjmp
#define setjmp  __builtin_setjmp
#define longjmp __builtin_longjmp
#define _JBLEN  5
#else
#include <setjmp.h>
#endif

typedef ptrdiff_t isize;
typedef uint8_t byte;

typedef struct Arena Arena;
struct Arena {
  byte **beg;
  byte *end;
  void **jmpbuf;
};

typedef enum {
  NULLOOM = 1 << 0,
  NOINIT = 1 << 1,
} ArenaFlags;

/** Usage:

  enum { ARENA_SIZE = 1 << 20 };
  autofree void *mem = malloc(ARENA_SIZE);
  Arena arena = NewArena(&(byte *){mem}, ARENA_SIZE);

  if (ArenaOOM(&arena)) {
    abort();
  }

  { // shadowed/nested arena within block scope
    PushArena(arena);

    This *x = New(&arena, This);
    This *y = foo(arena);

    // x, y should not assign to any variable outside of this block

    ...

    // PopArena(arena); // implicitly
  }

  That z[] = New(&arena, That, 10);

*/

#define New(...)                       ARENA_NEWX(__VA_ARGS__, ARENA_NEW4, ARENA_NEW3, ARENA_NEW2)(__VA_ARGS__)
#define ARENA_NEWX(a, b, c, d, e, ...) e
#define ARENA_NEW2(a, t)               (t *)arena_alloc(a, sizeof(t), _Alignof(t), 1, 0)
#define ARENA_NEW3(a, t, n)            (t *)arena_alloc(a, sizeof(t), _Alignof(t), n, 0)
#define ARENA_NEW4(a, t, n, z)                                     \
  (t *)_Generic((z), t *: arena_alloc_init, default: arena_alloc)( \
      a, sizeof(t), _Alignof(t), n, _Generic((z), t *: z, default: z))

#define ArenaOOM(A)                                \
  ({                                               \
    Arena *a_ = (A);                               \
    a_->jmpbuf = New(a_, void *, _JBLEN, NULLOOM); \
    !a_->jmpbuf || setjmp((void *)a_->jmpbuf);     \
  })

#define CONCAT0(A, B) A##B
#define CONCAT(A, B)  CONCAT0(A, B)

// PushArena leverages compound literal lifetime
// and variable shadowing in nested block scope
#define PushArena(NAME)                              \
  Arena CONCAT(NAME, __LINE__) = NAME;               \
  Arena NAME = CONCAT(NAME, __LINE__);               \
  NAME.beg = &(byte *){*CONCAT(NAME, __LINE__).beg}; \
  LogArena(NAME)
#define PopArena(NAME) LogArena(NAME)

#ifdef LOGGING
#define LogArena(A)                                                                           \
  fprintf(stderr, "%s:%d: Arena " #A "\tbeg=%ld->%ld end=%ld diff=%ld\n", __FILE__, __LINE__, \
          (uintptr_t)((A).beg), (uintptr_t)(*(A).beg), (uintptr_t)(A).end,                    \
          (isize)((A).end - (*(A).beg)))
#else
#define LogArena(A) ((void)A)
#endif

static inline Arena NewArena(byte **mem, isize size) {
  Arena a = {0};
  a.beg = mem;
  a.end = *mem ? *mem + size : 0;
  return a;
}

static inline void *arena_alloc(Arena *arena, isize size, isize align, isize count,
                                ArenaFlags flags) {
  assert(arena);

  byte *current = *arena->beg;
  isize avail = arena->end - current;
  isize padding = -(uintptr_t)current & (align - 1);
  if (count > (avail - padding) / size) {
    goto handle_oom;
  }

  isize total_size = size * count;
  *arena->beg += padding + total_size;
  byte *ret = current + padding;
  return flags & NOINIT ? ret : memset(ret, 0, total_size);

handle_oom:
  if (flags & NULLOOM || !arena->jmpbuf)
    return NULL;
#ifndef OOM
  longjmp((void *)arena->jmpbuf, 1);
#else
  assert(!OOM);
  abort();
#endif
}

static inline void *arena_alloc_init(Arena *arena, isize size, isize align, isize count,
                                     const void *const initptr) {
  assert(initptr);
  void *ptr = arena_alloc(arena, size, align, count, NOINIT);
  return memmove(ptr, initptr, size * count);
}

#define Push(S, A)                                                             \
  ({                                                                           \
    __typeof__(S) s_ = (S);                                                    \
    if (s_->len >= s_->cap) {                                                  \
      slice_grow(s_, sizeof(*s_->data), _Alignof(__typeof__(*s_->data)), (A)); \
    }                                                                          \
    s_->data + s_->len++;                                                      \
  })

static inline void slice_grow(void *slice, isize size, isize align, Arena *a) {
  struct {
    void *data;
    isize len;
    isize cap;
  } replica;
  memcpy(&replica, slice, sizeof(replica));

  const int grow = MAX_ALIGN;

  if (!replica.cap) {
    replica.cap = grow;
    replica.data = arena_alloc(a, size, align, replica.cap, 0);
  } else if ((uintptr_t)replica.data == (uintptr_t)*a->beg - size * replica.cap) {
    // grow inplace
    arena_alloc(a, size, 1, grow, 0);
    replica.cap += grow;
  } else {
    replica.cap += replica.cap / 2;  // grow by 1.5
    void *dest = arena_alloc(a, size, align, replica.cap, 0);
    void *src = replica.data;
    isize len = size * replica.len;
    memcpy(dest, src, len);
    replica.data = dest;
  }

  memcpy(slice, &replica, sizeof(replica));
}

// Arena owned str (aka astr)
typedef struct astr {
  char *data;
  ptrdiff_t len;
} astr;

// string literal only!
#define S(s) (astr){s, sizeof(s) - 1}

static inline astr astrclone(Arena *arena, astr s) {
  astr s2 = s;
  // Early return if string is empty or already at arena boundary
  if (!s.len || s.data + s.len == (char *)*arena->beg)
    return s2;

  s2.data = New(arena, char, s.len, NOINIT);
  if (s2.data >= s.data + s.len || s2.data + s2.len <= s.data) {
    memcpy(s2.data, s.data, s.len);
  } else {
    memmove(s2.data, s.data, s.len);
  }
  return s2;
}

static inline astr astrconcat(Arena *arena, astr head, astr tail) {
  astr ret = head;
  // Ignore empty head
  if (head.len == 0) {
    // If tail is at arena tip, return it directly; otherwise duplicate
    return tail.len && tail.data + tail.len == (char *)*arena->beg ? tail : astrclone(arena, tail);
  }
  // If head isn't at arena tip, duplicate it
  if (head.data + head.len != (char *)*arena->beg) {
    ret = astrclone(arena, head);
  }
  // Now head is guaranteed to be at arena tip, duplicate tail right after
  ret.len += astrclone(arena, tail).len;
  return ret;
}

static inline astr astrcopy(Arena *arena, const void *bytes, size_t nbytes) {
  return astrclone(arena, (astr){(char *)bytes, nbytes});
}

static inline astr astrappend(Arena *arena, astr head, const void *bytes, size_t nbytes) {
  return astrconcat(arena, head, (astr){(char *)bytes, nbytes});
}

static inline astr astrcpy(Arena *arena, const char *str) {
  return astrcopy(arena, str, strlen(str));
}

static inline astr astrcat(Arena *arena, astr head, const char *str) {
  return astrappend(arena, head, str, strlen(str));
}

static inline astr astrfmt(Arena *arena, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int nbytes = vsnprintf(NULL, 0, format, args);
  va_end(args);
  assert(nbytes >= 0);
  void *data = New(arena, char, nbytes + 1, NOINIT);
  va_start(args, format);
  int nbytes2 = vsnprintf(data, nbytes + 1, format, args);
  va_end(args);
  assert(nbytes2 == nbytes);
  arena->beg[0]--;
  return (astr){.data = data, .len = nbytes};
}

static inline bool astrcmp(astr a, astr b) {
  if (a.len != b.len)
    return false;

  return !a.len || !memcmp(a.data, b.data, a.len);
}

#endif  // ARENA_H

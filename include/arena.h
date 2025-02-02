#ifndef ARENA_H
#define ARENA_H

/** Credit:
    https://nullprogram.com/blog/2023/09/27/
    https://lists.sr.ht/~skeeto/public-inbox/%3C20231015233305.sssrgorhqu2qo5jr%40nullprogram.com%3E
    https://nullprogram.com/blog/2023/10/05/
*/

#include <memory.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define MAX_ALIGN _Alignof(max_align_t)

#if __has_include("debug.h")
#include "debug.h"
#else
#include <assert.h>
#endif

#ifdef __GNUC__
static void autofree_impl(void *p) { free(*((void **)p)); }
#define autofree __attribute__((__cleanup__(autofree_impl)))
#else
#warning "autofree is not supported"
#define autofree
#endif

#if defined(__GNUC__) && !defined(__APPLE__)
#define setjmp  __builtin_setjmp
#define longjmp __builtin_longjmp
#define _JBLEN  5
#else
#include <setjmp.h>
#endif

typedef ptrdiff_t ssize;
typedef unsigned char byte;

typedef struct Arena Arena;
struct Arena {
  byte **beg;
  byte *end;
  void **jmpbuf;
  Arena *parent;
};

enum {
  SOFTFAIL = 1 << 0,
  NOINIT = 1 << 1,
};

/** Usage:

  ssize cap = 1 << 20;
  void *heap = malloc(cap);
  Arena global = newarena(&(byte *){heap}, cap);

  if (ARENA_OOM(&global)) {
    abort();
  }

  Arena scratch = getscratch(&global);

  thing *x = New(&global, thing);
  thing *y = New(&scratch, thing);
  thing *z = helper(scratch);

  free(heap);

*/

#define New(...)                       ARENA_NEWX(__VA_ARGS__, ARENA_NEW4, ARENA_NEW3, ARENA_NEW2)(__VA_ARGS__)
#define ARENA_NEWX(a, b, c, d, e, ...) e
#define ARENA_NEW2(a, t)               (t *)arena_alloc(a, sizeof(t), _Alignof(t), 1, 0)
#define ARENA_NEW3(a, t, n)            (t *)arena_alloc(a, sizeof(t), _Alignof(t), n, 0)
#define ARENA_NEW4(a, t, n, z)         (t *)arena_alloc(a, sizeof(t), _Alignof(t), n, z)

#define ARENA_OOM(A)                                                           \
  ({                                                                           \
    Arena *a_ = (A);                                                           \
    a_->jmpbuf = New(a_, void *, _JBLEN, SOFTFAIL);                            \
    !a_->jmpbuf || setjmp(a_->jmpbuf);                                         \
  })

#define ARENA_PUSH(NAME)                                                       \
  Arena NAME_##__LINE__ = NAME;                                                \
  Arena NAME = NAME_##__LINE__;                                                \
  NAME.beg = &(byte *) { *(NAME_##__LINE__).beg }

#define Push(S, A)                                                             \
  ({                                                                           \
    __typeof__(S) s_ = (S);                                                    \
    if (s_->len >= s_->cap) {                                                  \
      slice_grow(s_, sizeof(*s_->data), _Alignof(__typeof__(*s_->data)), (A)); \
    }                                                                          \
    s_->data + s_->len++;                                                      \
  })

#ifdef LOGGING
#  define ARENA_LOG(A)                                                         \
     fprintf(stderr, "%s:%d: Arena " #A "\tbeg=%ld->%ld end=%ld diff=%ld\n",   \
             __FILE__,                                                         \
             __LINE__,                                                         \
            (uintptr_t)((A).beg), (uintptr_t)(*(A).beg),                       \
            (uintptr_t)(A).end,                                                \
            (ssize)((A).end - (*(A).beg)))
#else
#  define ARENA_LOG(A)   ((void)0)
#endif

static inline Arena newarena(byte **mem, ssize size) {
  Arena a = {0};
  a.beg = mem;
  a.end = *mem ? *mem + size : 0;
  return a;
}

static inline bool isscratch(Arena *a) {
  return !!a->parent;
}

static inline Arena getscratch(Arena *a) {
  if (isscratch(a)) return *a;

  Arena scratch = {0};
  scratch.beg = &a->end;
  scratch.end = *a->beg;
  scratch.jmpbuf = a->jmpbuf;
  scratch.parent = a;
  return scratch;
}

static inline void *arena_alloc(Arena *arena, ssize size, ssize align, ssize count, unsigned flags) {
  assert(arena);

  if (isscratch(arena)) {
    byte *newend = *arena->parent->beg;
    if (*arena->beg > newend) {
      arena->end = newend;
    } else {
      goto handle_oom;
    }
  }

  int is_forward = *arena->beg < arena->end;
  ssize avail = is_forward ? (arena->end - *arena->beg) : (*arena->beg - arena->end);
  ssize padding = (is_forward ? -1 : 1) * (uintptr_t)*arena->beg & (align - 1);
  bool oom = count > (avail - padding) / size;
  if (oom) {
    goto handle_oom;
  }

  // Calculate new position
  ssize total_size = size * count;
  ssize offset = (is_forward ? 1 : -1) * (padding + total_size);
  *arena->beg += offset;
  byte *ret = is_forward ? (*arena->beg - total_size) : *arena->beg;

  return flags & NOINIT ? ret : memset(ret, 0, total_size);

handle_oom:
  if (flags & SOFTFAIL || !arena->jmpbuf) return NULL;
#ifndef OOM
  longjmp(arena->jmpbuf, 1);
#else
  assert(!OOM);
  abort();
#endif
}

static inline void slice_grow(void *slice, ssize size, ssize align, Arena *a) {
  struct {
    void *data;
    ssize len;
    ssize cap;
  } replica;
  memcpy(&replica, slice, sizeof(replica));

  const int grow = 16;

  if (!replica.cap) {
    replica.cap = grow;
    replica.data = arena_alloc(a, size, align, replica.cap, 0);
  } else if ((*a->beg < a->end)          // bump upwards
          && ((uintptr_t)replica.data == // grow inplace
              (uintptr_t)*a->beg - size * replica.cap)) {
    arena_alloc(a, size, 1, grow, 0);
    replica.cap += grow;
  } else {
    replica.cap += replica.cap / 2;     // grow by 1.5
    void *dest = arena_alloc(a, size, align, replica.cap, 0);
    void *src = replica.data;
    ssize len = size * replica.len;
    memcpy(dest, src, len);
    replica.data = dest;
  }

  memcpy(slice, &replica, sizeof(replica));
}

#ifdef GLOBAL_ARENA
extern Arena *GLOBAL_ARENA;

#define CONCAT0(a, b) a ## b
#define CONCAT(a, b)  CONCAT0(a, b)

#define GLOBAL_ARENA_MALLOC   CONCAT(GLOBAL_ARENA, _malloc)
#define GLOBAL_ARENA_CALLOC   CONCAT(GLOBAL_ARENA, _calloc)
#define GLOBAL_ARENA_REALLOC  CONCAT(GLOBAL_ARENA, _realloc)
#define GLOBAL_ARENA_FREE     CONCAT(GLOBAL_ARENA, _free)

void *GLOBAL_ARENA_MALLOC(size_t sz) {
  assert(GLOBAL_ARENA);
  return arena_alloc(GLOBAL_ARENA, sz, MAX_ALIGN, 1, NOINIT);
}

void *GLOBAL_ARENA_CALLOC(size_t num, size_t sz) {
  assert(GLOBAL_ARENA);
  return arena_alloc(GLOBAL_ARENA, sz, MAX_ALIGN, num, 0);
}

void *GLOBAL_ARENA_REALLOC(void *src, size_t sz) {
  if (!sz) return NULL;
  void *dest = GLOBAL_ARENA_MALLOC(sz);
  return src ? memmove(dest, src, sz): dest;
}

void GLOBAL_ARENA_FREE(void *ptr) {}
#endif // GLOBAL_ARENA

// STC allocator
static inline void *arena_realloc(Arena *a, void *old_p, ssize old_sz, ssize sz, unsigned flags) {
  if (!old_p) return arena_alloc(a, sz, MAX_ALIGN, 1, flags);

  if (sz <= old_sz) return old_p; // no-op

  // grow inplace
  if((*a->beg < a->end) && (uintptr_t)old_p == (uintptr_t)*a->beg - old_sz) {
    void *p = arena_alloc(a, sz - old_sz, 1, 1, flags);
    assert(p - old_p == old_sz);
    return old_p;
  }

  void *p = arena_alloc(a, sz, MAX_ALIGN, 1, flags);
  return p ? memcpy(p, old_p, old_sz) : NULL;
}

#define arena_malloc(sz)              arena_alloc(self->aux.arena, (sz), MAX_ALIGN, 1, NOINIT)
#define arena_calloc(n, sz)           arena_alloc(self->aux.arena, (sz), MAX_ALIGN, (n), 0)
#define arena_realloc(p, old_sz, sz)  arena_realloc(self->aux.arena, (p), (old_sz), (sz), NOINIT)
#define arena_free(p, sz)             (void)0

#endif  // ARENA_H

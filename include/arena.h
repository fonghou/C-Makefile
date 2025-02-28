/**
 * @file arena.h
 * @brief A fast, region-based memory allocator with optional commit-on-demand support.
 *
 * This arena allocator provides a simple, efficient way to manage memory in a contiguous
 * region. It supports both immediate allocation and commit-on-demand via mmap (when
 * OOM_COMMIT is defined). Key features:
 * - Fast allocation with minimal overhead
 * - Optional zero-initialization
 * - Slice and string utilities
 * - OOM handling via setjmp/longjmp or NULL return
 *
 * Credit:
 * - https://nullprogram.com/blog/2023/09/27/
 * - https://nullprogram.com/blog/2023/10/05/
 * - https://www.chiark.greenend.org.uk/~sgtatham/quasiblog/c11-generic/#inline
 */

#ifndef ARENA_H_
#define ARENA_H_

#include <memory.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef OOM_COMMIT
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#if defined(__GNUC__) && !defined(__APPLE__)
#undef setjmp
#define setjmp  __builtin_setjmp
#define longjmp __builtin_longjmp
#define _JBLEN  5
#else
#include <setjmp.h>
#endif

#ifdef __clang__
#define TRAP(c) __builtin_debugtrap();
#elif defined(__x86_64__)
#define TRAP(c) __asm__ volatile("int3; nop");
#elif defined(__GNUC__)
#define TRAP(c) __builtin_trap();
#elif defined(_MSC_VER)
#define TRAP(c) __debugbreak();
#else
#include <signal.h>
#define TRAP(c) raise(SIGTRAP);
#endif

#define Assert(c)                                                                                       \
  do {                                                                                                  \
    if (!(c)) {                                                                                         \
      fprintf(stderr, "Assertion failed: %s in function %s %s:%d\n", #c, __func__, __FILE__, __LINE__); \
      TRAP();                                                                                           \
    }                                                                                                   \
  } while (0)

// Branch optimization macros.
#ifdef __GNUC__
#define ARENA_LIKELY(xp)   __builtin_expect((bool)(xp), true)
#define ARENA_UNLIKELY(xp) __builtin_expect((bool)(xp), false)
#else
#define ARENA_LIKELY(xp)   (xp)
#define ARENA_UNLIKELY(xp) (xp)
#endif

// Ensures inlining if possible.
#if defined(__GNUC__)
#define ARENA_INLINE static inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define ARENA_INLINE static inline __forceinline
#else
#define ARENA_INLINE static inline
#endif

typedef ptrdiff_t isize;

#define countof(arr) ((isize)(sizeof(arr) / sizeof((arr)[0])))

// - https://gcc.gnu.org/bugzilla/show_bug.cgi?id=66110
// - https://software.codidact.com/posts/280966
typedef unsigned char byte;

typedef struct Arena Arena;
struct Arena {
  byte *beg;
  byte *end;
  void **jmpbuf;
#ifdef OOM_COMMIT
  isize commit_size;
#endif
};

enum {
  _NO_INIT = 1u << 0,   // don't `zero` alloced memory
  _OOM_NULL = 1u << 1,  // return NULL on OOM
};

typedef struct {
  unsigned mask;
} ArenaFlag;

static const ArenaFlag NO_INIT = {_NO_INIT};
static const ArenaFlag OOM_NULL = {_OOM_NULL};

#define MAX_ALIGN _Alignof(max_align_t)

#ifndef ARENA_COMMIT_PAGE_COUNT
#define ARENA_COMMIT_PAGE_COUNT 1
#endif

#define KB(n) (((size_t)(n)) << 10)
#define MB(n) (((size_t)(n)) << 20)
#define GB(n) (((size_t)(n)) << 30)
#define TB(n) (((size_t)(n)) << 40)

/** Usage:

#define ARENA_SIZE MB(128)

#ifdef OOM_COMMIT
  void *mem = mmap(0, ARENA_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  Arena arena[] = { arena_init(mem, 0) };
#else
  autofree void *mem = malloc(ARENA_SIZE);
  Arena arena[] = { arena_init(mem, ARENA_SIZE) };
#endif

  if (ArenaOOM(arena)) {
    abort();
  }

  {
    Scratch(arena);

    Ty *x = New(arena, Ty);

    // x pointer cannot escape this block

  }

*/

#define New(...)                  _NEWX(__VA_ARGS__, _NEW4, _NEW3, _NEW2)(__VA_ARGS__)
#define _NEWX(a, b, c, d, e, ...) e
#define _NEW2(a, t)               (t *)arena_alloc(a, sizeof(t), _Alignof(t), 1, (ArenaFlag){0})
#define _NEW3(a, t, n)            (t *)arena_alloc(a, sizeof(t), _Alignof(t), n, (ArenaFlag){0})
#define _NEW4(a, t, n, z)                                                                         \
  (t *)_Generic((z), t *: arena_alloc_init, ArenaFlag: arena_alloc)(a, sizeof(t), _Alignof(t), n, \
                                                                    _Generic((z), t *: z, ArenaFlag: z))

#define ArenaOOM(A)                                 \
  ({                                                \
    Arena *a_ = (A);                                \
    a_->jmpbuf = New(a_, void *, _JBLEN, OOM_NULL); \
    !a_->jmpbuf || setjmp((void *)a_->jmpbuf);      \
  })

#define CONCAT_(a, b) a##b
#define CONCAT(a, b)  CONCAT_(a, b)
#define ARENA_TMP     CONCAT(_arena_, __LINE__)

#define Scratch(arena)      \
  Arena *ARENA_TMP = arena; \
  Arena arena[] = {*ARENA_TMP}

#define Slice(...)                   _SliceX(__VA_ARGS__, _Slice4, _Slice3, _Slice2)(__VA_ARGS__)
#define _SliceX(a, b, c, d, e, ...)  e
#define _Slice2(arena, slice)        _Slice3(arena, slice, 0)
#define _Slice3(arena, slice, start) _Slice4(arena, slice, start, slice.len - (start))
#define _Slice4(arena, slice, start, length)                                     \
  ({                                                                             \
    Assert(start >= 0 && "slice start must be non-negative");                    \
    Assert(length >= 0 && "slice length must be non-negative");                  \
    Assert((start) + (length) <= slice.len && "Invalid slice range");            \
    __typeof__(slice) s_ = slice;                                                \
    s_.cap = s_.len = length;                                                    \
    if (s_.len == 0) {                                                           \
      s_.data = NULL;                                                            \
    }                                                                            \
    if (s_.data) {                                                               \
      s_.data = New(arena, __typeof__(s_.data[0]), s_.len, (s_.data + (start))); \
    }                                                                            \
    s_;                                                                          \
  })

#define Push(slice, arena)                                                         \
  ({                                                                               \
    Assert((slice)->len >= 0 && "slice.len must be non-negative");                 \
    Assert((slice)->cap >= 0 && "slice.cap must be non-negative");                 \
    Assert(!((slice)->len == 0 && (slice)->data != NULL) && "Invalid slice");      \
    __typeof__(slice) s_ = slice;                                                  \
    if (s_->len >= s_->cap) {                                                      \
      slice_grow(s_, sizeof(*s_->data), _Alignof(__typeof__(*s_->data)), (arena)); \
    }                                                                              \
    s_->data + s_->len++;                                                          \
  })

#ifdef __GNUC__
static void autofree_impl(void *p) {
  free(*((void **)p));
}
#define autofree __attribute__((__cleanup__(autofree_impl)))
#else
#warning "autofree is not supported on your compiler, use free()"
#define autofree
#endif

ARENA_INLINE Arena arena_init(byte *mem, isize size) {
  Arena a = {0};
#ifdef OOM_COMMIT
  size = sysconf(_SC_PAGESIZE) * ARENA_COMMIT_PAGE_COUNT;
  mprotect(mem, size, PROT_READ | PROT_WRITE);
  a.commit_size = size;
#endif
  Assert(size > 0 && "try build with -DOOM_COMMIT");
  a.beg = mem;
  a.end = mem ? mem + size : 0;
  return a;
}

static void *arena_alloc(Arena *arena, isize size, isize align, isize count, ArenaFlag flags) {
  Assert(arena != NULL && "arena cannot be NULL");
  Assert(count >= 0 && "count must be positive");

  byte *current = arena->beg;
  isize avail = arena->end - current;
  isize padding = -(uintptr_t)current & (align - 1);
  while (ARENA_UNLIKELY(count >= (avail - padding) / size)) {
#ifdef OOM_COMMIT
    if (mprotect(arena->end, arena->commit_size, PROT_READ | PROT_WRITE) == -1) {
      perror("arena_alloc mprotect");
      goto handle_oom;
    }
    arena->end += arena->commit_size;
    avail = arena->end - current;
    continue;
#endif
    goto handle_oom;
  }

  isize total_size = size * count;
  arena->beg += padding + total_size;
  current += padding;
  return flags.mask & _NO_INIT ? current : memset(current, 0, total_size);

handle_oom:
  if (flags.mask & _OOM_NULL)
    return NULL;
#ifdef OOM_TRAP
  Assert(!OOM_TRAP);
#endif
  Assert(arena->jmpbuf && "not set by ArenaOOM");
  longjmp((void *)arena->jmpbuf, 1);
}

ARENA_INLINE void *arena_alloc_init(Arena *arena, isize size, isize align, isize count,
                                    const void *const initptr) {
  Assert(initptr != NULL && "initptr cannot be NULL");
  void *ptr = arena_alloc(arena, size, align, count, NO_INIT);
  memmove(ptr, initptr, size * count);
  return ptr;
}

ARENA_INLINE void slice_grow(void *slice, isize size, isize align, Arena *arena) {
  struct {
    void *data;
    isize len;
    isize cap;
  } slicemeta;
  memcpy(&slicemeta, slice, sizeof(slicemeta));

  const int grow = MAX_ALIGN;

  if (slicemeta.cap == 0) {
    // handle slice initialized on stack
    slicemeta.cap = slicemeta.len + grow;
    void *ptr = arena_alloc(arena, size, align, slicemeta.cap, NO_INIT);
    // copy from stack or no-op if slicemeta.len == 0
    slicemeta.data = memcpy(ptr, slicemeta.data, size * slicemeta.len);
  } else if (ARENA_LIKELY((uintptr_t)slicemeta.data == (uintptr_t)arena->beg - size * slicemeta.cap)) {
    // grow slice inplace
    slicemeta.cap += grow;
    arena_alloc(arena, size, 1, grow, NO_INIT);
  } else {
    slicemeta.cap += slicemeta.cap / 2;  // grow by 1.5
    void *ptr = arena_alloc(arena, size, align, slicemeta.cap, NO_INIT);
    // move slice from possible overlapping arena
    slicemeta.data = memmove(ptr, slicemeta.data, size * slicemeta.len);
  }

  memcpy(slice, &slicemeta, sizeof(slicemeta));
}

/** Usage:

static inline void *vt_arena_malloc(size_t size, arena **ctx) {
  return arena_malloc(size, *ctx);
}

static inline void vt_arena_free(void *ptr, size_t size, arena **ctx) {
  arena_free(ptr, size, *ctx);
}

#define NAME      Map_int_astr
#define KEY_TY    int
#define VAL_TY    astr
#define CTX_TY    Arena *
#define MALLOC_FN vt_arena_malloc
#define FREE_FN   vt_arena_free
#include "verstable.h"

*/

ARENA_INLINE void *arena_malloc(size_t size, Arena *arena) {
  return arena_alloc(arena, size, MAX_ALIGN, 1, NO_INIT);
}

ARENA_INLINE void arena_free(void *ptr, size_t size, Arena *arena) {
  Assert(arena != NULL && "arena cannot be NULL");
  if (!ptr)
    return;
  if ((uintptr_t)ptr == (uintptr_t)arena->beg - size) {
    arena->beg = ptr;
  }
}

// Arena owned str (aka astr)
typedef struct astr {
  char *data;
  isize len;
} astr;

// string literal only!
#define astr(s) ((astr){s, sizeof(s) - 1})

// printf("%.*s", S(s))
#define S(s) (int)(s).len, (s).data

ARENA_INLINE astr astrclone(Arena *arena, astr s) {
  astr s2 = s;
  // Early return if string is empty or already at arena boundary
  if (!s.len || s.data + s.len == (char *)arena->beg)
    return s2;

  s2.data = New(arena, char, s.len, NO_INIT);
  memmove(s2.data, s.data, s.len);
  return s2;
}

ARENA_INLINE astr astrconcat(Arena *arena, astr head, astr tail) {
  astr ret = head;
  // Ignore empty head
  if (head.len == 0) {
    // If tail is at arena tip, return it directly; otherwise clone
    return tail.len && tail.data + tail.len == (char *)arena->beg ? tail : astrclone(arena, tail);
  }
  // If head isn't at arena tip, clone it
  if (head.data + head.len != (char *)arena->beg) {
    ret = astrclone(arena, head);
  }
  // Now head is guaranteed to be at arena tip, clone tail and append it
  ret.len += astrclone(arena, tail).len;
  return ret;
}

ARENA_INLINE astr astrcopy(Arena *arena, const void *bytes, size_t nbytes) {
  return astrclone(arena, (astr){(char *)bytes, nbytes});
}

ARENA_INLINE astr astrappend(Arena *arena, astr head, const void *bytes, size_t nbytes) {
  return astrconcat(arena, head, (astr){(char *)bytes, nbytes});
}

ARENA_INLINE astr astrcpy(Arena *arena, const char *str) {
  return astrcopy(arena, str, strlen(str));
}

ARENA_INLINE astr astrcat(Arena *arena, astr head, const char *str) {
  return astrappend(arena, head, str, strlen(str));
}

static astr astrfmt(Arena *arena, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int nbytes = vsnprintf(NULL, 0, format, args);
  va_end(args);
  Assert(nbytes >= 0);
  void *data = New(arena, char, nbytes + 1, NO_INIT);
  va_start(args, format);
  int nbytes2 = vsnprintf(data, nbytes + 1, format, args);
  va_end(args);
  Assert(nbytes2 == nbytes);
  arena->beg--;
  return (astr){.data = data, .len = nbytes};
}

ARENA_INLINE bool astrcmp(astr a, astr b) {
  if (a.len != b.len)
    return false;

  return !a.len || !memcmp(a.data, b.data, a.len);
}

ARENA_INLINE uint64_t astrhash(astr key) {
  uint64_t hash = 0xcbf29ce484222325ull;
  for (isize i = 0; i < key.len; i++)
    hash = ((unsigned char)key.data[i] ^ hash) * 0x100000001b3ull;

  return hash;
}

#if __has_include("cc.h")
#include "cc.h"
#define CC_CMPR astr, return strncmp(val_1.data, val_2.data, val_1.len < val_2.len ? val_1.len : val_2.len);
#define CC_HASH astr, return astrhash(val);
#endif

#endif  // ARENA_H_

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

#define _GNU_SOURCE
#include <memory.h>
#include <setjmp.h>
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

// Branch optimization macros.
#ifdef __GNUC__
#define ARENA_LIKELY(xp)   __builtin_expect((bool)(xp), true)
#define ARENA_UNLIKELY(xp) __builtin_expect((bool)(xp), false)
#else
#define ARENA_LIKELY(xp)   (xp)
#define ARENA_UNLIKELY(xp) (xp)
#endif

#ifdef __clang__
#define DEBUG_TRAP() __builtin_debugtrap();
#elif defined(__x86_64__)
#define DEBUG_TRAP() __asm__("int3; nop");
#elif defined(__GNUC__)
#define DEBUG_TRAP() __builtin_trap();
#else
#include <signal.h>
#define DEBUG_TRAP() raise(SIGTRAP);
#endif

#ifndef NDEBUG
#define ASSERT_LOG(c) fprintf(stderr, "Assertion failed: " c " at %s %s:%d\n", __func__, __FILE__, __LINE__)
#else
#define ASSERT_LOG(c) (void)0
#endif

#define Assert(c)               \
  do {                          \
    if (ARENA_UNLIKELY(!(c))) { \
      ASSERT_LOG(#c);           \
      DEBUG_TRAP();             \
    }                           \
  } while (0)

#define Min(a, b) ((a) < (b) ? (a) : (b))
#define Max(a, b) ((a) > (b) ? (a) : (b))

#define KB(n) (((size_t)(n)) << 10)
#define MB(n) (((size_t)(n)) << 20)
#define GB(n) (((size_t)(n)) << 30)
#define TB(n) (((size_t)(n)) << 40)

typedef ptrdiff_t isize;

#define countof(arr) ((isize)(sizeof(arr) / sizeof((arr)[0])))

// - https://gcc.gnu.org/bugzilla/show_bug.cgi?id=66110
// - https://software.codidact.com/posts/280966
typedef unsigned char byte;

typedef struct Arena Arena;
struct Arena {
  byte *init;
  byte *beg;
  byte *end;
  jmp_buf *jmpbuf;
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

/** Usage:

#define ARENA_SIZE MB(128)

#ifdef OOM_COMMIT
  Arena arena[] = { arena_init(0, 0) };
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

#define ArenaOOM(arena)      \
  ({                         \
    jmp_buf jmpbuf;          \
    arena->jmpbuf = &jmpbuf; \
    setjmp(jmpbuf);          \
  })

#define CONCAT_(a, b) a##b
#define CONCAT(a, b)  CONCAT_(a, b)
#define ARENA_TMP     CONCAT(_arena_, __LINE__)

#define Scratch(arena)      \
  Arena *ARENA_TMP = arena; \
  Arena arena[] = {*ARENA_TMP}

#define Vec(T)     \
  struct Vec_##T { \
    T *data;       \
    isize len;     \
    isize cap;     \
  }

#define Slice(...)                   _SliceX(__VA_ARGS__, _Slice4, _Slice3, _Slice2)(__VA_ARGS__)
#define _SliceX(a, b, c, d, e, ...)  e
#define _Slice2(arena, slice)        _Slice3(arena, slice, 0)
#define _Slice3(arena, slice, start) _Slice4(arena, slice, start, slice.len - (start))
#define _Slice4(arena, slice, start, length)                                     \
  ({                                                                             \
    Assert(start >= 0 && "slice start must be non-negative");                    \
    Assert(length >= 0 && "slice length must be non-negative");                  \
    Assert(slice.len >= (start) + (length) && "Invalid slice range");            \
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

// Ensures inlining if possible.
#if defined(__GNUC__)
#define ARENA_INLINE static inline __attribute__((always_inline))
#else
#define ARENA_INLINE static inline
#endif

#ifndef ARENA_COMMIT_PAGE_COUNT
#define ARENA_COMMIT_PAGE_COUNT 16
#endif

#define ARENA_RESERVE_PAGE_COUNT (1000000 * ARENA_COMMIT_PAGE_COUNT)

ARENA_INLINE Arena arena_init(byte *mem, isize size) {
  Arena a = {0};
#ifdef OOM_COMMIT
  if (size == 0) {
    isize page_size = sysconf(_SC_PAGESIZE);
    a.commit_size = size = page_size * ARENA_COMMIT_PAGE_COUNT;
    if (mem == NULL) {
      mem = mmap(0, page_size * ARENA_RESERVE_PAGE_COUNT, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      if (mem == MAP_FAILED) {
        perror("arena_init mmap");
        Assert(!"arena_init mmap");
      }
    }
    Assert(!mprotect(mem, size, PROT_READ | PROT_WRITE));
  }
#endif
  a.init = a.beg = mem;
  a.end = mem ? mem + size : 0;
  return a;
}

ARENA_INLINE void arena_reset(Arena *arena) {
  arena->beg = arena->init;
}

static void *arena_alloc(Arena *arena, isize size, isize align, isize count, ArenaFlag flags) {
  Assert(arena != NULL && "arena cannot be NULL");
  Assert(size > 0 && "size must be positive");
  Assert(count > 0 && "count must be positive");

  byte *current = arena->beg;
  isize avail = arena->end - current;
  isize pad = -(uintptr_t)current & (align - 1);
  while (ARENA_UNLIKELY(count >= (avail - pad) / size)) {
#ifdef OOM_COMMIT
    // arena->commit_size == 0 if arena was malloced
    if (arena->commit_size) {
      if (mprotect(arena->end, arena->commit_size, PROT_READ | PROT_WRITE) == -1) {
        perror("arena_alloc mprotect");
        goto HANDLE_OOM;
      }
      arena->end += arena->commit_size;
      avail = arena->end - current;
      continue;
    }
#endif
    goto HANDLE_OOM;
  }

  isize total_size = size * count;
  arena->beg += pad + total_size;
  current += pad;
  return flags.mask & _NO_INIT ? current : memset(current, 0, total_size);

HANDLE_OOM:
  if (flags.mask & _OOM_NULL)
    return NULL;
#ifdef OOM_TRAP
  Assert(!OOM_TRAP);
#endif
  Assert(arena->jmpbuf && "not set by ArenaOOM");
  longjmp(*arena->jmpbuf, 1);
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

  const int grow = 16;

  if (slicemeta.cap == 0) {
    slicemeta.cap = slicemeta.len + grow;
    void *ptr = arena_alloc(arena, size, align, slicemeta.cap, NO_INIT);
    slicemeta.data = memmove(ptr, slicemeta.data, size * slicemeta.len);
  } else if (ARENA_LIKELY((uintptr_t)slicemeta.data == (uintptr_t)arena->beg - size * slicemeta.cap)) {
    // grow slice inplace
    slicemeta.cap += grow;
    arena_alloc(arena, size, 1, grow, NO_INIT);
  } else {
    slicemeta.cap += Max(slicemeta.cap / 2, grow);
    void *ptr = arena_alloc(arena, size, align, slicemeta.cap, NO_INIT);
    slicemeta.data = memmove(ptr, slicemeta.data, size * slicemeta.len);
  }

  memcpy(slice, &slicemeta, sizeof(slicemeta));
}

ARENA_INLINE void *arena_malloc(size_t size, Arena *arena) {
  return arena_alloc(arena, size, _Alignof(max_align_t), 1, NO_INIT);
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
#define astr(s) ((astr){(s), sizeof(s) - 1})

// printf("%.*s", S(s))
#define S(s) (int)(s).len, (s).data

ARENA_INLINE astr astr_clone(Arena *arena, astr s) {
  astr s2 = s;
  // Early return if string is empty or already at arena boundary
  if (s.len == 0 || s.data + s.len == (char *)arena->beg)
    return s2;

  s2.data = New(arena, char, s.len, NO_INIT);
  memmove(s2.data, s.data, s.len);
  return s2;
}

ARENA_INLINE astr astr_concat(Arena *arena, astr head, astr tail) {
  astr result = head;
  // Ignore empty head
  if (head.len == 0) {
    // If tail is at arena tip, return it directly; otherwise clone
    return tail.len && tail.data + tail.len == (char *)arena->beg ? tail : astr_clone(arena, tail);
  }
  // If head isn't at arena tip, clone it
  if (head.data + head.len != (char *)arena->beg) {
    result = astr_clone(arena, head);
  }
  // Now head is guaranteed to be at arena tip, clone tail and append it
  result.len += astr_clone(arena, tail).len;
  return result;
}

ARENA_INLINE astr astr_from_bytes(Arena *arena, const void *bytes, size_t nbytes) {
  return astr_clone(arena, (astr){(char *)bytes, nbytes});
}

ARENA_INLINE astr astr_from_cstr(Arena *arena, const char *str) {
  return astr_from_bytes(arena, str, strlen(str));
}

ARENA_INLINE astr astr_cat_bytes(Arena *arena, astr head, const void *bytes, size_t nbytes) {
  return astr_concat(arena, head, (astr){(char *)bytes, nbytes});
}

ARENA_INLINE astr astr_cat_cstr(Arena *arena, astr head, const char *str) {
  return astr_cat_bytes(arena, head, str, strlen(str));
}

static astr astr_format(Arena *arena, const char *format, ...) {
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
  // drop \0 so that astr_concat still works
  arena->beg--;
  return (astr){.data = data, .len = nbytes};
}

// just like strndup
ARENA_INLINE char *astr_cstrdup(astr s) {
  return strndup(s.data, s.len);
}

// return a null-terminated byte string with a temporary lifetime.
ARENA_INLINE const char *astr_to_cstr(Arena arena, astr s) {
  return astr_concat(&arena, s, astr("\0")).data;
}

ARENA_INLINE astr _astr_split_by_char(astr s, const char *charset, isize *pos, Arena *a) {
  astr slice = {s.data + *pos, s.len - *pos};
  const char *p1 = astr_to_cstr(*a, slice);
  const char *p2 = strpbrk(p1, charset);
  astr token = {slice.data, p2 ? (p2 - p1) : slice.len};
  isize sep_len = p2 ? strspn(p2, charset) : 0;  // skip seperator found in charset
  *pos += token.len + sep_len;
  return token;
}

// for (astr_split_by_char(it, ",| ", str, arena)) { ... it.token ...}
#define astr_split_by_char(it, charset, str, arena) \
  struct {                                          \
    astr input, token;                              \
    const char *sep;                                \
    isize pos;                                      \
  } it = {.input = str, .sep = charset};            \
  it.pos < it.input.len && (it.token = _astr_split_by_char(it.input, it.sep, &it.pos, arena)).data[0];

ARENA_INLINE astr _astr_split(astr s, astr sep, isize *pos) {
  astr slice = {s.data + *pos, s.len - *pos};
  const char *res = memmem(slice.data, slice.len, sep.data, sep.len);
  astr token = {slice.data, res && res != slice.data ? (res - slice.data) : slice.len};
  *pos += token.len + sep.len;
  return token;
}

// for (astr_split(it, ", ", str)) { ... it.token ...}
#define astr_split(it, strsep, str)                             \
  struct {                                                      \
    astr input, token, sep;                                     \
    isize pos;                                                  \
  } it = {.input = str, .sep = (astr){strsep, strlen(strsep)}}; \
  it.pos < it.input.len && (it.token = _astr_split(it.input, it.sep, &it.pos)).data;

ARENA_INLINE bool astr_equals(astr a, astr b) {
  if (a.len != b.len)
    return false;

  return !a.len || !memcmp(a.data, b.data, a.len);
}

ARENA_INLINE bool astr_starts_with(astr s, astr prefix) {
  isize n = prefix.len;
  return n <= s.len && !memcmp(s.data, prefix.data, n);
}

ARENA_INLINE bool astr_ends_with(astr s, astr suffix) {
  isize n = suffix.len;
  return n <= s.len && !memcmp(s.data + s.len - n, suffix.data, n);
}

ARENA_INLINE astr astr_trim_left(astr s) {
  while (s.len && *s.data <= ' ')
    ++s.data, --s.len;
  return s;
}

ARENA_INLINE astr astr_trim_right(astr s) {
  while (s.len && s.data[s.len - 1] <= ' ')
    --s.len;
  return s;
}

ARENA_INLINE astr astr_trim(astr sv) {
  return astr_trim_right(astr_trim_left(sv));
}

ARENA_INLINE uint64_t astr_hash(astr key) {
  uint64_t hash = 0xcbf29ce484222325ull;
  for (isize i = 0; i < key.len; i++)
    hash = ((unsigned char)key.data[i] ^ hash) * 0x100000001b3ull;

  return hash;
}

/** Usage:

#include "cc.h"

static inline uint64_t astr_wyhash(astr key) {
  return cc_wyhash(key.data, key.len);
}

static inline void *vt_arena_malloc(size_t size, Arena **ctx) {
  return arena_malloc(size, *ctx);
}

static inline void vt_arena_free(void *ptr, size_t size, Arena **ctx) {
  arena_free(ptr, size, *ctx);
}

#define NAME      Map_astr_astr
#define KEY_TY    astr
#define VAL_TY    astr
#define CTX_TY    Arena *
#define CMPR_FN   astr_equals
#define HASH_FN   astr_wyhash
#define MALLOC_FN vt_arena_malloc
#define FREE_FN   vt_arena_free
#include "verstable.h"

*/

#endif  // ARENA_H_

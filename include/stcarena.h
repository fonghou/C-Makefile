#ifndef STC_ARENA_H
#define STC_ARENA_H

#include "arena.h"

static inline void *arena_realloc(Arena *a, void *old_p, isize old_sz, isize sz, unsigned flags) {
  if (!old_p)
    return arena_alloc(a, sz, MAX_ALIGN, 1, flags);

  if (sz <= old_sz)
    return old_p;  // shrink inplace

  // grow inplace
  if ((uintptr_t)old_p == (uintptr_t)a->beg - old_sz) {
    void *p = arena_alloc(a, sz - old_sz, 1, 1, flags);
    assert(p - old_p == old_sz);
    return old_p;
  }

  void *p = arena_alloc(a, sz, MAX_ALIGN, 1, flags);
  return p ? memcpy(p, old_p, old_sz) : NULL;
}

#define arena_malloc(sz)             arena_alloc(self->aux.arena, (sz), MAX_ALIGN, 1, NO_INIT)
#define arena_calloc(n, sz)          arena_alloc(self->aux.arena, (sz), MAX_ALIGN, (n), 0)
#define arena_realloc(p, old_sz, sz) arena_realloc(self->aux.arena, (p), (old_sz), (sz), NO_INIT)
#define arena_free(p, sz)            (void)0

#endif

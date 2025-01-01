#if !defined(NDEBUG) && !defined(__COSMOCC__) && __has_include("elf.h")
#define __linux__
#define UPRINTF_IMPLEMENTATION
#include "uprintf.h"
#else
#define uprintf(fmt, ...) (void)0;
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define GLOBAL_ARENA _global_arena
#include "arena.h"
#include "list.h"
#include "tstr.h"

#define NAME ssmap
#define KEY_TY char *
#define VAL_TY char *
#include "verstable.h"

typedef struct {
  int64_t *data;
  ptrdiff_t len;
  ptrdiff_t cap;
} int64s;

typedef struct {
  int64s data;
  struct list_head list_node;
} int64s_list;

int main(void) {
#ifdef __COSMOCC__
  ShowCrashReports();
#endif

  enum { cap = 1 << 12 };
  autofree byte *heap = gc(malloc(cap));
  Arena arena = newarena(&(byte *){heap}, cap);
  GLOBAL_ARENA = &arena;
  tstr_set_allocator(&GLOBAL_ARENA_MALLOC_FN, &GLOBAL_ARENA_FREE_FN);
  ARENA_LOG(arena);

  if (ARENA_OOM(&arena)) {
    fputs("!!! OOM exit !!!\n", stderr);
    exit(1);
  }

  {
    Arena local = arena;
    ARENA_PUSH(local, arena);
    // local.beg = &(byte *){*arena.beg};

    ARENA_LOG(local);

    struct list_head *mylist = New(&local, struct list_head);
    INIT_LIST_HEAD(mylist);

    int64s_list *i64s = New(&local, int64s_list);
    list_add(&i64s->list_node, mylist);

    ARENA_LOG(local);

    Arena scratch = getscratch(&local);
    ARENA_LOG(scratch);

    int64s *fibs = &i64s->data;
    fibs->cap = 64;
    fibs->data = New(&local, int64_t, fibs->cap);
    *Push(fibs, &local) = 0;
    *Push(fibs, &local) = 1;
    for (int i = 2; i < 80; ++i) {
      Arena a;
      if (i % 2 == 0) {
        a = scratch;
      } else {
        a = local;
      }
      *Push(fibs, &a) = fibs->data[i - 2] + fibs->data[i - 1];
    }

    int64s_list *pos;
    list_for_each_entry(pos, mylist, list_node) {
      int64s *entry = (int64s *)pos;
      for (int i = 0; i < entry->len; ++i)
        printf("%ld ", entry->data[i]);
      printf("\nfibs %td:%td\n", entry->cap, entry->len);
    }

    ARENA_LOG(scratch);
    ARENA_LOG(local);
  }

  ARENA_LOG(arena);
  ssmap mymap;
  vt_init_with_ctx(&mymap, &arena);

  enum { sz = 100 };
  for (int i = 0; i < 10; ++i) {
    char *k = New(&arena, char, sz);
    snprintf(k, sz, "key-%d", i);
    char *v = New(&arena, char, sz);
    int n = snprintf(v, sz, "%d", 10000 + i);
    ssmap_itr it = vt_insert(&mymap, k, v);
    uprintf("%S\n", &it);
  }

  for (int i = 0; i < 100; ++i) {
    tstr *k = tstr_from_format("key-%d", i);
    ssmap_itr it = vt_get(&mymap, k);
    tstr_free(k);
    if (vt_is_end(it)) {
      char *k0 = "key-0";
      it = vt_get(&mymap, k0);
      printf("%s found %s!\n", k0, it.data->val);
      break;
    }
    printf("%s found %s!\n", it.data->key, it.data->val);
  }

  for (ssmap_itr it = vt_first(&mymap); !vt_is_end(it); it = vt_next(it)) {
    printf("%s, %s\n", it.data->key, it.data->val);
  }

  ARENA_LOG(arena);

  vt_cleanup(&mymap);

#ifndef __GNUC__
  free(heap);
#endif
}

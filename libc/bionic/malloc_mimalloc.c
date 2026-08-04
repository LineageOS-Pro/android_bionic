/*
 * Copyright (C) 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Thin compatibility layer between bionic's MallocDispatch interface and the
// mimalloc allocator (https://github.com/microsoft/mimalloc, main branch).
//
// mimalloc's public API on the main branch no longer provides mallinfo,
// mallopt, malloc_info, malloc_iterate, malloc_disable or malloc_enable, all of
// which bionic's dispatch table requires. They are re-implemented here on top
// of mimalloc's option/stats/collect/visit APIs. The remaining entry points
// (mi_malloc, mi_free, ...) are provided directly by the mimalloc library.
//
// This file is plain C so that <mimalloc.h> can be included without pulling in
// the C++ std::allocator templates (which need a C++ standard library that
// bionic's libc must not link against).

#include <errno.h>
#include <malloc.h>
#include <stdbit.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <mimalloc.h>

// The glibc-compatible memalign(3) rounds non-power-of-two boundaries up to the
// next power of two, whereas mimalloc rejects them with NULL. Wrap it to keep
// that behaviour (see also the matching #define in malloc_mimalloc.h).
void* mi_memalign_round_up_boundary(size_t boundary, size_t size) {
  boundary = stdc_bit_ceil(boundary);
  return mi_memalign(boundary, size);
}

// mi_malloc_usable_size is only compiled by mimalloc when MI_MALLOC_OVERRIDE is
// defined, which bionic deliberately does not use (it would export the
// malloc/free override symbols). Provide it here on top of the
// always-available mi_usable_size.
size_t mi_malloc_usable_size(const void* p) {
  return mi_usable_size(p);
}

// mallinfo() is deprecated; mimalloc does not expose per-block accounting
// through its public API on the main branch, so report the committed bytes
// (a superset of the allocated bytes) in the in-use/mmapped fields.
struct mallinfo mi_mallinfo(void) {
  struct mallinfo mi = {0};
  size_t current_commit = 0;
  size_t peak_commit = 0;
  mi_process_info(NULL, NULL, NULL, NULL, NULL, &current_commit, &peak_commit, NULL);
  mi.hblkhd = current_commit;
  mi.uordblks = current_commit;
  return mi;
}

int mi_mallopt(int param, int value) {
  switch (param) {
    case M_DECAY_TIME:
      // M_DECAY_TIME is expressed in milliseconds (-1 disables purging), which
      // matches mimalloc's mi_option_purge_delay semantics.
      mi_option_set(mi_option_purge_delay, value);
      return 1;
    case M_PURGE:
    case M_PURGE_ALL:
      mi_collect(true);
      return 1;
    case M_LOG_STATS:
      mi_stats_print_out(NULL, NULL);
      return 1;
    default:
      return 0;
  }
}

int mi_malloc_info(int options, FILE* fp) {
  if (options != 0) {
    errno = EINVAL;
    return -1;
  }

  fflush(fp);
  int fd = fileno(fp);

  size_t current_commit = 0;
  size_t peak_commit = 0;
  mi_process_info(NULL, NULL, NULL, NULL, NULL, &current_commit, &peak_commit, NULL);

  // Keep the XML shape compatible with the format written by the jemalloc and
  // scudo wrappers.
  dprintf(fd, "<malloc version=\"mimalloc-1\"><heap nr=\"0\">");
  dprintf(fd, "<allocated-large>%zu</allocated-large>", current_commit);
  dprintf(fd, "<allocated-huge>0</allocated-huge>");
  dprintf(fd, "<allocated-bins>0</allocated-bins>");
  dprintf(fd, "</heap></malloc>");
  return 0;
}

struct MiIterateContext {
  uintptr_t base;
  size_t size;
  void (*callback)(uintptr_t, size_t, void*);
  void* arg;
};

// Visits one allocated block; reports it to the caller if it falls inside the
// requested [base, base + size) range.
static bool MiBlockVisitor(const mi_heap_t* heap, const mi_heap_area_t* area, void* block,
                           size_t block_size, void* arg) {
  (void)heap;
  (void)area;
  struct MiIterateContext* ctx = (struct MiIterateContext*)arg;
  uintptr_t block_addr = (uintptr_t)block;
  if (block_addr >= ctx->base && block_addr < ctx->base + ctx->size) {
    ctx->callback(block_addr, block_size, ctx->arg);
  }
  return true;  // continue iterating
}

// mimalloc has no API to iterate over all threads' heaps; visit the blocks of
// the calling thread's default heap as a best effort (used by libmemunreachable
// and other debugging tools).
int mi_malloc_iterate(uintptr_t base, size_t size,
                      void (*callback)(uintptr_t base, size_t size, void* arg), void* arg) {
  if (callback == NULL) {
    errno = EINVAL;
    return -1;
  }
  struct MiIterateContext ctx = {base, size, callback, arg};
  // dev3 renamed mi_heap_* to mi_theap_* for the default allocator entry.
  mi_theap_visit_blocks(mi_theap_get_default(), true /* visit_blocks */, MiBlockVisitor, &ctx);
  return 0;
}

// mimalloc has no equivalent of malloc_disable/malloc_enable (used by
// debugging tools to pause allocation activity); no-ops.
void mi_malloc_disable(void) {}
void mi_malloc_enable(void) {}

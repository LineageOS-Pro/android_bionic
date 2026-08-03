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

#pragma once

#include <malloc.h>  // For struct mallinfo.
#include <stdint.h>
#include <stdio.h>
#include <sys/cdefs.h>

// Need to wrap memalign since mi_memalign fails on non-power of 2 alignments,
// while the glibc-compatible memalign(3) rounds the boundary up to the next
// power of 2.
#define mi_memalign mi_memalign_round_up_boundary

__BEGIN_DECLS

// Functions provided by the mimalloc static library (external/mimalloc).
void* mi_malloc(size_t size);
void* mi_calloc(size_t count, size_t size);
void* mi_realloc(void* p, size_t newsize);
void mi_free(void* p);
size_t mi_malloc_usable_size(const void* p);
void* mi_memalign_round_up_boundary(size_t boundary, size_t size);
int mi_posix_memalign(void** p, size_t alignment, size_t size);
void* mi_aligned_alloc(size_t alignment, size_t size);
void* mi_valloc(size_t size);
void* mi_pvalloc(size_t size);

// Compatibility layer implemented in bionic (malloc_mimalloc.cpp); these
// functions were removed from mimalloc's public API on the main branch.
struct mallinfo mi_mallinfo();
int mi_mallopt(int param, int value);
int mi_malloc_info(int options, FILE* fp);
int mi_malloc_iterate(uintptr_t base, size_t size,
                      void (*callback)(uintptr_t base, size_t size, void* arg), void* arg);
void mi_malloc_disable();
void mi_malloc_enable();

__END_DECLS

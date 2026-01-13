/*
 * This file is part of AtomVM.
 *
 * Copyright 2025 Mateusz Furga <mateusz.furga@swmansion.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0 OR LGPL-2.1-or-later
 */

#ifndef _POPCORN_ETS_MULTIMAP_H_
#define _POPCORN_ETS_MULTIMAP_H_

#include "../globalcontext.h"
#include "../term.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NUM_BUCKETS 16

typedef enum EtsMultimapType
{
    EtsMultimapTypeSingle, // Only one value per key
    EtsMultimapTypeSet,    // Only unique values per key
    EtsMultimapTypeList    // Allow duplicate values per key
} EtsMultimapType;

typedef enum EtsMultimapStatus
{
    EtsMultimapOk,
    EtsMultimapKeyExists,
    EtsMultimapAllocationError
} EtsMultimapStatus;

typedef struct EtsMultimap
{
    EtsMultimapType type;
    size_t key_index;
    struct EtsMultimapNode *buckets[NUM_BUCKETS];
} EtsMultimap;

typedef struct EtsMultimapNode
{
    struct EtsMultimapNode *next;
    struct EtsMultimapEntry *entries;
} EtsMultimapNode;

typedef struct EtsMultimapEntry
{
    struct EtsMultimapEntry *next;
    term tuple;
    Heap *heap;
} EtsMultimapEntry;

/**
 * @brief Create a new multimap.
 *
 * @param type the multimap type
 * @param index the index of the tuple element to use as key
 * @return the created multimap, or NULL on error
 */
EtsMultimap *ets_multimap_new(EtsMultimapType type, size_t index);

/**
 * @brief Delete the multimap and all its contents.
 *
 * @param multimap the multimap
 * @param global the global context
 */
void ets_multimap_delete(EtsMultimap *multimap, GlobalContext *global);

/**
 * @brief Insert one or more tuples into the multimap.
 *
 * @param multimap the multimap
 * @param tuples the tuples to insert
 * @param count the number of tuples to insert
 * @return EtsMultimapOk on success, otherwise an error status
 *
 * @note Terms passed to this function will be copied to the ETS heap.
 */
EtsMultimapStatus ets_multimap_insert(
    EtsMultimap *multimap,
    term *tuples,
    size_t count,
    GlobalContext *global);

/**
 * @brief Lookup tuples by key.
 *
 * @param multimap the multimap
 * @param key the key to lookup
 * @param[out] tuples the found tuples (or NULL to only get the count)
 * @param[out] count the number of found tuples; must not be NULL
 * @param global the global context
 * @return EtsMultimapOk on success, otherwise an error status
 *
 * @note Terms returned by this function come from the ETS heap and should be copied
 *       to the process heap if needed.
 * @warning The caller is responsible for freeing the memory pointed to by `tuples`
 *          using `free()`. When count is zero, memory is not allocated and `tuples` is set to NULL.
 */
EtsMultimapStatus ets_multimap_lookup(
    EtsMultimap *multimap,
    term key,
    term **tuples,
    size_t *count,
    GlobalContext *global);

/**
 * @brief Remove all tuples with the given key.
 *
 * @param multimap the multimap
 * @param key the key to lookup
 * @param global the global context
 * @return EtsMultimapOk on success, otherwise an error status
 */
EtsMultimapStatus ets_multimap_remove(
    EtsMultimap *multimap,
    term key,
    GlobalContext *global);

#ifdef __cplusplus
}
#endif

#endif // _POPCORN_ETS_MULTIMAP_H_

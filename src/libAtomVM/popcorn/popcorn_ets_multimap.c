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

#include <assert.h>

#include "../globalcontext.h"
#include "../term.h"

#include "popcorn_ets_multimap.h"
#include "popcorn_ets_multimap_hash.h"

#define DYNARRAY_INITIAL_CAPACITY 8
#define DYNARRAY_GROWTH_FACTOR 2

static EtsMultimapEntry *entry_new(term tuple);
static void entry_delete(EtsMultimapEntry *entry, GlobalContext *global);
static EtsMultimapNode *node_new(EtsMultimapNode *next, EtsMultimapEntry *entries);
static void node_delete(EtsMultimapNode *node, GlobalContext *global);
static PopcornEtsStatus node_find(
    EtsMultimap *multimap,
    term key,
    EtsMultimapNode **out_node,
    GlobalContext *global);
static term node_key(EtsMultimap *multimap, EtsMultimapNode *node);
static void multimap_to_single(EtsMultimap *multimap, GlobalContext *global);
static void insert_revert(
    EtsMultimap *multimap,
    EtsMultimapEntry **entries,
    size_t count,
    GlobalContext *global);
static PopcornEtsStatus tuple_exists(
    EtsMultimapNode *node,
    term tuple,
    bool *exists,
    GlobalContext *global);

EtsMultimap *ets_multimap_new(EtsMultimapType type, size_t key_index)
{
    EtsMultimap *multimap = malloc(sizeof(struct EtsMultimap));
    if (IS_NULL_PTR(multimap)) {
        return NULL;
    }

    multimap->type = type;
    multimap->key_index = key_index;

    for (size_t i = 0; i < NUM_BUCKETS; i++) {
        multimap->buckets[i] = NULL;
    }

    return multimap;
}

void ets_multimap_delete(EtsMultimap *multimap, GlobalContext *global)
{
    for (size_t i = 0; i < NUM_BUCKETS; i++) {
        EtsMultimapNode *node = multimap->buckets[i];
        while (node != NULL) {
            EtsMultimapNode *next = node->next;
            node_delete(node, global);
            node = next;
        }
    }
    free(multimap);
}

PopcornEtsStatus ets_multimap_lookup(
    EtsMultimap *multimap,
    term key,
    term **tuples,
    size_t *count,
    GlobalContext *global)
{
    assert(count != NULL);

    *count = 0;

    EtsMultimapNode *node;
    PopcornEtsStatus result = node_find(multimap, key, &node, global);
    if (UNLIKELY(result == PopcornEtsAllocationError)) {
        return result;
    }

    if (node == NULL) {
        return PopcornEtsOk;
    }

    assert(node->entries != NULL);

    for (EtsMultimapEntry *iter = node->entries; iter != NULL; iter = iter->next) {
        (*count)++;
    }

    if (tuples == NULL) {
        // only return number of tuples found
        return PopcornEtsOk;
    }

    *tuples = malloc(sizeof(term) * (*count));
    if (IS_NULL_PTR(*tuples)) {
        return PopcornEtsAllocationError;
    }

    size_t i = 0;
    for (EtsMultimapEntry *iter = node->entries; iter != NULL; iter = iter->next, i++) {
        assert(i < *count);
        (*tuples)[i] = iter->tuple;
    }

    return PopcornEtsOk;
}

PopcornEtsStatus ets_multimap_insert(
    EtsMultimap *multimap,
    term *tuples,
    size_t count,
    GlobalContext *global)
{
    if (tuples == NULL || count == 0) {
        return PopcornEtsOk;
    }

    EtsMultimapEntry **entries = malloc(sizeof(EtsMultimapEntry *) * count);
    if (IS_NULL_PTR(entries)) {
        return PopcornEtsAllocationError;
    }

    for (size_t i = 0; i < count; i++) {
        entries[i] = entry_new(tuples[i]);
        if (IS_NULL_PTR(entries[i])) {
            for (size_t j = 0; j < i; j++) {
                entry_delete(entries[j], global);
            }
            free(entries);
            return PopcornEtsAllocationError;
        }
    }

    PopcornEtsStatus status = PopcornEtsOk;

    for (size_t i = 0; i < count; i++) {
        EtsMultimapEntry *entry = entries[i];
        term key = term_get_tuple_element(entry->tuple, multimap->key_index);

        EtsMultimapNode *node;
        if (UNLIKELY(node_find(multimap, key, &node, global) == PopcornEtsAllocationError)) {
            status = PopcornEtsAllocationError;
            break;
        }

        if (node == NULL) {
            EtsMultimapNode *new_node = node_new(NULL, entry);
            if (IS_NULL_PTR(new_node)) {
                status = PopcornEtsAllocationError;
                break;
            }

            assert(new_node->entries != NULL);

            uint32_t idx = hash_term(key, global) % NUM_BUCKETS;
            new_node->next = multimap->buckets[idx];
            multimap->buckets[idx] = new_node;
            continue;
        }

        assert(node->entries != NULL);

        if (multimap->type == EtsMultimapTypeSet) {
            bool exists;

            if (UNLIKELY(tuple_exists(node, entry->tuple, &exists, global) == PopcornEtsAllocationError)) {
                status = PopcornEtsAllocationError;
                break;
            }

            if (exists) {
                entry_delete(entry, global);
                entries[i] = NULL;
                continue;
            }
        }

        entry->next = node->entries;
        node->entries = entry;
    }

    if (status != PopcornEtsOk) {
        insert_revert(multimap, entries, count, global);
    } else if (multimap->type == EtsMultimapTypeSingle) {
        multimap_to_single(multimap, global);
    }

    free(entries);

    return status;
}

PopcornEtsStatus ets_multimap_remove(
    EtsMultimap *multimap,
    term key,
    GlobalContext *global)
{
    EtsMultimapNode *node;
    if (UNLIKELY(node_find(multimap, key, &node, global) == PopcornEtsAllocationError)) {
        return PopcornEtsAllocationError;
    }

    if (node == NULL) {
        return PopcornEtsOk;
    }

    assert(node->entries != NULL);
    assert(term_compare(key, node_key(multimap, node), TermCompareExact, global) == TermEquals);

    uint32_t idx = hash_term(key, global) % NUM_BUCKETS;
    EtsMultimapNode *iter = multimap->buckets[idx];
    EtsMultimapNode *prev = NULL;

    while (iter) {
        if (iter == node) {
            if (prev == NULL) {
                multimap->buckets[idx] = iter->next;
            } else {
                prev->next = iter->next;
            }
            break;
        }
        prev = iter;
        iter = iter->next;
    }

    node_delete(node, global);

    return PopcornEtsOk;
}

PopcornEtsStatus ets_multimap_remove_tuple(
    EtsMultimap *multimap,
    term tuple,
    GlobalContext *global)
{
    assert(term_is_tuple(tuple));

    if (multimap->key_index >= (size_t) term_get_tuple_arity(tuple)) {
        return PopcornEtsBadEntry;
    }

    term key = term_get_tuple_element(tuple, multimap->key_index);

    EtsMultimapNode *node;
    if (UNLIKELY(node_find(multimap, key, &node, global) == PopcornEtsAllocationError)) {
        return PopcornEtsAllocationError;
    }

    if (node == NULL) {
        return PopcornEtsOk;
    }

    assert(node->entries != NULL);

    size_t capacity = DYNARRAY_INITIAL_CAPACITY;
    size_t count = 0;

    EtsMultimapEntry **to_remove = malloc(sizeof(EtsMultimapEntry *) * capacity);
    if (IS_NULL_PTR(to_remove)) {
        return PopcornEtsAllocationError;
    }

    for (EtsMultimapEntry *iter = node->entries; iter != NULL; iter = iter->next) {
        TermCompareResult result = term_compare(tuple, iter->tuple, TermCompareExact, global);

        if (UNLIKELY(result == TermCompareMemoryAllocFail)) {
            free(to_remove);
            return PopcornEtsAllocationError;
        }

        if (result == TermEquals) {
            if (count >= capacity) {
                capacity *= DYNARRAY_GROWTH_FACTOR;
                EtsMultimapEntry **new_to_remove = realloc(to_remove, sizeof(EtsMultimapEntry *) * capacity);
                if (IS_NULL_PTR(new_to_remove)) {
                    free(to_remove);
                    return PopcornEtsAllocationError;
                }
                to_remove = new_to_remove;
            }
            to_remove[count++] = iter;
        }
    }

    EtsMultimapEntry *prev = NULL;
    for (EtsMultimapEntry *iter = node->entries; iter != NULL; iter = iter->next) {
        bool removed = false;
        for (size_t i = 0; i < count; i++) {
            if (iter == to_remove[i]) {
                if (prev == NULL) {
                    node->entries = iter->next;
                } else {
                    prev->next = iter->next;
                }
                removed = true;
                break;
            }
        }
        if (!removed) {
            prev = iter;
        }
    }

    for (size_t i = 0; i < count; i++) {
        entry_delete(to_remove[i], global);
    }

    if (node->entries == NULL) {
        uint32_t idx = hash_term(key, global) % NUM_BUCKETS;

        EtsMultimapNode *prev_node = NULL;
        for (EtsMultimapNode *iter = multimap->buckets[idx]; iter != NULL; prev_node = iter, iter = iter->next) {
            if (iter == node) {
                if (prev_node == NULL) {
                    multimap->buckets[idx] = iter->next;
                } else {
                    prev_node->next = iter->next;
                }
                break;
            }
        }

        node_delete(node, global);
    }

    free(to_remove);

    return PopcornEtsOk;
}

static PopcornEtsStatus node_find(
    EtsMultimap *multimap,
    term key,
    EtsMultimapNode **out_node,
    GlobalContext *global)
{
    assert(out_node != NULL);

    *out_node = NULL;

    uint32_t idx = hash_term(key, global) % NUM_BUCKETS;
    EtsMultimapNode *node = multimap->buckets[idx];

    while (node) {
        TermCompareResult result = term_compare(key, node_key(multimap, node), TermCompareExact, global);

        if (UNLIKELY(result == TermCompareMemoryAllocFail)) {
            return PopcornEtsAllocationError;
        }

        if (result == TermEquals) {
            *out_node = node;
            return PopcornEtsOk;
        }

        node = node->next;
    }

    return PopcornEtsOk;
}

static void insert_revert(
    EtsMultimap *multimap,
    EtsMultimapEntry **entries,
    size_t count,
    GlobalContext *global)
{
    for (size_t i = 0; i < NUM_BUCKETS; i++) {
        EtsMultimapNode *node = multimap->buckets[i];

        while (node != NULL) {
            EtsMultimapNode *next_node = node->next;
            EtsMultimapEntry *entry = node->entries;

            assert(entry != NULL);

            while (entry != NULL) {
                EtsMultimapEntry *next_entry = entry->next;

                for (size_t j = 0; j < count; j++) {
                    if (entry == entries[j]) {
                        node->entries = next_entry;
                    }
                }

                entry = next_entry;
            }

            if (node->entries == NULL) {
                multimap->buckets[i] = next_node;
                node_delete(node, global);
            }

            node = next_node;
        }
    }

    for (size_t i = 0; i < count; i++) {
        if (entries[i] != NULL) {
            entry_delete(entries[i], global);
        }
    }
}

static void multimap_to_single(EtsMultimap *multimap, GlobalContext *global)
{
    for (size_t i = 0; i < NUM_BUCKETS; i++) {
        for (EtsMultimapNode *node = multimap->buckets[i]; node != NULL; node = node->next) {
            assert(node->entries != NULL);
            EtsMultimapEntry *entry = node->entries->next;

            while (entry != NULL) {
                EtsMultimapEntry *next = entry->next;
                entry_delete(entry, global);
                entry = next;
            }

            node->entries->next = NULL;
        }
    }
}

static PopcornEtsStatus tuple_exists(
    EtsMultimapNode *node,
    term tuple,
    bool *exists,
    GlobalContext *global)
{
    for (EtsMultimapEntry *iter = node->entries; iter != NULL; iter = iter->next) {
        TermCompareResult result = term_compare(tuple, iter->tuple, TermCompareExact, global);

        if (UNLIKELY(result == TermCompareMemoryAllocFail)) {
            return PopcornEtsAllocationError;
        }

        if (result == TermEquals) {
            *exists = true;
            return PopcornEtsOk;
        }
    }

    *exists = false;
    return PopcornEtsOk;
}

static term node_key(EtsMultimap *multimap, EtsMultimapNode *node)
{
    EtsMultimapEntry *entry = node->entries;
    assert(entry != NULL);
    return term_get_tuple_element(entry->tuple, multimap->key_index);
}

static EtsMultimapNode *node_new(EtsMultimapNode *next, EtsMultimapEntry *entries)
{
    EtsMultimapNode *node = malloc(sizeof(EtsMultimapNode));
    if (IS_NULL_PTR(node)) {
        return NULL;
    }
    node->next = next;
    node->entries = entries;
    return node;
}

static EtsMultimapEntry *entry_new(term tuple)
{
    EtsMultimapEntry *entry = malloc(sizeof(EtsMultimapEntry));
    if (IS_NULL_PTR(entry)) {
        return NULL;
    }

    Heap *heap = malloc(sizeof(Heap));
    if (IS_NULL_PTR(heap)) {
        free(entry);
        return NULL;
    }

    size_t size = memory_estimate_usage(tuple);
    if (UNLIKELY(memory_init_heap(heap, size) != MEMORY_GC_OK)) {
        free(entry);
        free(heap);
        return NULL;
    }

    tuple = memory_copy_term_tree(heap, tuple);

    entry->tuple = tuple;
    entry->heap = heap;
    entry->next = NULL;

    return entry;
}

static void node_delete(EtsMultimapNode *node, GlobalContext *global)
{
    EtsMultimapEntry *entry = node->entries;

    while (entry != NULL) {
        EtsMultimapEntry *next = entry->next;
        entry_delete(entry, global);
        entry = next;
    }

    free(node);
}

static void entry_delete(EtsMultimapEntry *entry, GlobalContext *global)
{
    memory_destroy_heap(entry->heap, global);
    free(entry->heap);
    free(entry);
}

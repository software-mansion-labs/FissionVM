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

#include <stdlib.h>
#include <string.h>

#include "popcorn_ets_multimap.h"
#include "popcorn_ets_multimap_hash.h"
#include "../smp.h"
#include "../term.h"
#include "../utils.h"

static struct EtsMultimapNode *ets_multimap_node_new(struct EtsMultimapNode *next, struct EtsMultimapEntry *entries);
static struct EtsMultimapEntry *ets_multimap_entry_new(term tuple);
static void ets_multimap_node_delete(struct EtsMultimapNode *node, GlobalContext *global);
static void ets_multimap_entry_delete(struct EtsMultimapEntry *entry, GlobalContext *global);
static EtsMultimapStatus ets_multimap_find_node(
    EtsMultimap *multimap,
    term key,
    struct EtsMultimapNode **out_node,
    GlobalContext *global);
static void ets_multimap_to_one(EtsMultimap *multimap, GlobalContext *global);
static void ets_multimap_revert_insert(
    EtsMultimap *multimap,
    struct EtsMultimapEntry **entries,
    size_t count,
    GlobalContext *global);
static EtsMultimapStatus ets_multimap_tuple_exists(
    struct EtsMultimapNode *node,
    term tuple,
    bool *exists,
    GlobalContext *global);
static term node_key(EtsMultimap *multimap, struct EtsMultimapNode *node);

EtsMultimap *ets_multimap_new(EtsMultimapType type, size_t index)
{
    EtsMultimap *multimap = malloc(sizeof(EtsMultimap));
    if (IS_NULL_PTR(multimap)) {
        return NULL;
    }

    multimap->type = type;
    multimap->index = index;

    for (size_t i = 0; i < NUM_BUCKETS; i++) {
        multimap->buckets[i] = NULL;
    }

    return multimap;
}

void ets_multimap_delete(EtsMultimap *multimap, GlobalContext *global)
{
    for (size_t i = 0; i < NUM_BUCKETS; i++) {
        struct EtsMultimapNode *node = multimap->buckets[i];
        while (node != NULL) {
            struct EtsMultimapNode *next = node->next;
            ets_multimap_node_delete(node, global);
            node = next;
        }
    }
    free(multimap);
}

EtsMultimapStatus ets_multimap_insert(
    EtsMultimap *multimap,
    term *tuples,
    size_t count,
    GlobalContext *global)
{
    if (tuples == NULL || count == 0) {
        return EtsMultimapOk;
    }

    struct EtsMultimapEntry **entries = malloc(sizeof(struct EtsMultimapEntry *) * count);
    if (IS_NULL_PTR(entries)) {
        return EtsMultimapAllocationError;
    }

    for (size_t i = 0; i < count; i++) {
        entries[i] = ets_multimap_entry_new(tuples[i]);
        if (IS_NULL_PTR(entries[i])) {
            for (size_t j = 0; j < i; j++) {
                ets_multimap_entry_delete(entries[j], global);
            }
            free(entries);
            return EtsMultimapAllocationError;
        }
    }

    EtsMultimapStatus status = EtsMultimapOk;
    bool error = false;

    for (size_t i = 0; i < count; i++) {
        struct EtsMultimapEntry *entry = entries[i];
        term key = term_get_tuple_element(entry->tuple, multimap->index);

        struct EtsMultimapNode *node;
        if (ets_multimap_find_node(multimap, key, &node, global) == EtsMultimapAllocationError) {
            error = true;
            status = EtsMultimapAllocationError;
            break;
        }

        if (node == NULL) {
            struct EtsMultimapNode *new_node = ets_multimap_node_new(NULL, entry);
            if (IS_NULL_PTR(new_node)) {
                error = true;
                status = EtsMultimapAllocationError;
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

            if (ets_multimap_tuple_exists(node, entry->tuple, &exists, global) == EtsMultimapAllocationError) {
                error = true;
                status = EtsMultimapAllocationError;
                break;
            }

            if (!exists) {
                entry->next = node->entries;
                node->entries = entry;
            }
        } else {
            entry->next = node->entries;
            node->entries = entry;
        }
    }

    if (error) {
        ets_multimap_revert_insert(multimap, entries, count, global);
    } else if (multimap->type == EtsMultimapTypeOne) {
        ets_multimap_to_one(multimap, global);
    }

    free(entries);

    return status;
}

EtsMultimapStatus ets_multimap_lookup(
    EtsMultimap *multimap,
    term key,
    term **tuples,
    size_t *count,
    GlobalContext *global)
{
    assert(count != NULL);
    *count = 0;

    EtsMultimapStatus result;
    struct EtsMultimapNode *node;
    if ((result = ets_multimap_find_node(multimap, key, &node, global)) == EtsMultimapAllocationError) {
        return result;
    }

    if (node == NULL) {
        return EtsMultimapOk;
    }

    assert(node->entries != NULL);

    for (struct EtsMultimapEntry *iter = node->entries; iter != NULL; iter = iter->next) {
        (*count)++;
    }

    if (tuples == NULL) {
        // only return number of tuples found
        return EtsMultimapOk;
    }

    *tuples = malloc(sizeof(term) * (*count));
    if (IS_NULL_PTR(*tuples)) {
        return EtsMultimapAllocationError;
    }

    int i = *count - 1;
    for (struct EtsMultimapEntry *iter = node->entries; iter != NULL; iter = iter->next, i--) {
        assert(i >= 0);
        (*tuples)[i] = iter->tuple;
    }

    return EtsMultimapOk;
}

EtsMultimapStatus ets_multimap_remove(
    EtsMultimap *multimap,
    term key,
    GlobalContext *global)
{
    struct EtsMultimapNode *node;
    if (ets_multimap_find_node(multimap, key, &node, global) == EtsMultimapAllocationError) {
        return EtsMultimapAllocationError;
    }

    if (node == NULL) {
        return EtsMultimapOk;
    }

    assert(node->entries != NULL);
    assert(term_compare(key, node_key(multimap, node), TermCompareExact, global) == TermEquals);

    uint32_t idx = hash_term(key, global) % NUM_BUCKETS;
    struct EtsMultimapNode *iter = multimap->buckets[idx];
    struct EtsMultimapNode *prev = NULL;

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

    ets_multimap_node_delete(node, global);

    return EtsMultimapOk;
}

static EtsMultimapStatus ets_multimap_find_node(
    EtsMultimap *multimap,
    term key,
    struct EtsMultimapNode **out_node,
    GlobalContext *global)
{
    uint32_t idx = hash_term(key, global) % NUM_BUCKETS;
    struct EtsMultimapNode *node = multimap->buckets[idx];

    if (node == NULL) {
        *out_node = NULL;
        return EtsMultimapOk;
    }

    while (node) {
        TermCompareResult result = term_compare(key, node_key(multimap, node), TermCompareExact, global);

        if (result == TermCompareMemoryAllocFail) {
            return EtsMultimapAllocationError;
        }

        if (result == TermEquals) {
            *out_node = node;
            return EtsMultimapOk;
        }

        node = node->next;
    }

    *out_node = NULL;
    return EtsMultimapOk;
}

static void ets_multimap_revert_insert(
    EtsMultimap *multimap,
    struct EtsMultimapEntry **entries,
    size_t count,
    GlobalContext *global)
{
    for (size_t idx = 0; idx < NUM_BUCKETS; idx++) {
        struct EtsMultimapNode *node = multimap->buckets[idx];

        while (node != NULL) {
            struct EtsMultimapNode *next_node = node->next;
            struct EtsMultimapEntry *entry = node->entries;

            assert(entry != NULL);

            while (entry != NULL) {
                struct EtsMultimapEntry *next_entry = entry->next;

                for (size_t j = 0; j < count; j++) {
                    if (entry == entries[j]) {
                        node->entries = next_entry;
                    }
                }

                entry = next_entry;
            }

            if (node->entries == NULL) {
                multimap->buckets[idx] = next_node;
                ets_multimap_node_delete(node, global);
            }

            node = next_node;
        }
    }

    for (size_t i = 0; i < count; i++) {
        ets_multimap_entry_delete(entries[i], global);
    }
}

static void ets_multimap_to_one(EtsMultimap *multimap, GlobalContext *global)
{
    for (size_t i = 0; i < NUM_BUCKETS; i++) {
        for (struct EtsMultimapNode *node = multimap->buckets[i]; node != NULL; node = node->next) {
            assert(node->entries != NULL);
            struct EtsMultimapEntry *entry = node->entries->next;

            while (entry != NULL) {
                struct EtsMultimapEntry *next = entry->next;
                ets_multimap_entry_delete(entry, global);
                entry = next;
            }

            node->entries->next = NULL;
        }
    }
}

static EtsMultimapStatus ets_multimap_tuple_exists(
    struct EtsMultimapNode *node,
    term tuple,
    bool *exists,
    GlobalContext *global)
{
    for (struct EtsMultimapEntry *iter = node->entries; iter != NULL; iter = iter->next) {
        TermCompareResult result = term_compare(tuple, iter->tuple, TermCompareExact, global);
        if (result == TermCompareMemoryAllocFail) {
            return EtsMultimapAllocationError;
        }

        if (result == TermEquals) {
            *exists = true;
            return EtsMultimapOk;
        }
    }

    *exists = false;
    return EtsMultimapOk;
}

static term node_key(EtsMultimap *multimap, struct EtsMultimapNode *node)
{
    struct EtsMultimapEntry *entry = node->entries;
    return entry != NULL ? term_get_tuple_element(entry->tuple, multimap->index) : term_nil();
}

static struct EtsMultimapNode *ets_multimap_node_new(struct EtsMultimapNode *next, struct EtsMultimapEntry *entries)
{
    struct EtsMultimapNode *node = malloc(sizeof(struct EtsMultimapNode));
    if (IS_NULL_PTR(node)) {
        return NULL;
    }
    node->next = next;
    node->entries = entries;
    return node;
}

static struct EtsMultimapEntry *ets_multimap_entry_new(term tuple)
{
    struct EtsMultimapEntry *entry = malloc(sizeof(struct EtsMultimapEntry));
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

static void ets_multimap_node_delete(struct EtsMultimapNode *node, GlobalContext *global)
{
    struct EtsMultimapEntry *entry = node->entries;

    while (entry != NULL) {
        struct EtsMultimapEntry *next = entry->next;
        ets_multimap_entry_delete(entry, global);
        entry = next;
    }

    free(node);
}

static void ets_multimap_entry_delete(struct EtsMultimapEntry *entry, GlobalContext *global)
{
    memory_destroy_heap(entry->heap, global);
    free(entry);
}

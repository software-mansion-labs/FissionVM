/*
 * This file is part of AtomVM.
 *
 * Copyright 2024 Fred Dushin <fred@dushin.nt>
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

#include <stdint.h>

#include "../context.h"
#include "../defaultatoms.h"
#include "../list.h"
#include "../memory.h"
#include "../term.h"
#include "../utils.h"

#include "popcorn_ets2.h"
#include "popcorn_ets_multimap.h"

#define ETS_ANY_PROCESS -1

#ifndef AVM_NO_SMP
#include "../smp.h"
#define SMP_RDLOCK(table) smp_rwlock_rdlock(table->lock)
#define SMP_WRLOCK(table) smp_rwlock_wrlock(table->lock)
#define SMP_UNLOCK(table) smp_rwlock_unlock(table->lock)
#else
#define SMP_RDLOCK(htable) UNUSED(htable)
#define SMP_WRLOCK(htable) UNUSED(htable)
#define SMP_UNLOCK(htable) UNUSED(htable)
#endif

struct Popcorn2EtsTable
{
    struct ListHead head;

    term name;
    bool named;
    size_t key_index;
    Popcorn2EtsTableType type;
    Popcorn2EtsTableAccess access;

    EtsMultimap *multimap;

    int32_t owner_process_id;
    uint64_t ref_ticks;

#ifndef AVM_NO_SMP
    RWLock *lock;
#endif
};

typedef enum TableAccess
{
    TableAccessNone,
    TableAccessRead,
    TableAccessWrite
} TableAccess;

static struct Popcorn2EtsTable *get_table(
    Popcorn2Ets *ets,
    term name_or_ref,
    int32_t process_id,
    TableAccess access);
static void add_table(Popcorn2Ets *ets, struct Popcorn2EtsTable *table);
static void delete_all_tables(Popcorn2Ets *ets, GlobalContext *global);
static void table_destroy(struct Popcorn2EtsTable *table, GlobalContext *global);

static Popcorn2EtsStatus insert_one(
    struct Popcorn2EtsTable *table,
    term tuple,
    bool new,
    Context *ctx);
static Popcorn2EtsStatus insert_many(
    struct Popcorn2EtsTable *table,
    term tuples,
    bool new,
    Context *ctx);

void popcorn2_ets_init(Popcorn2Ets *ets)
{
    synclist_init(&ets->ets_tables);
}

void popcorn2_ets_destroy(Popcorn2Ets *ets, GlobalContext *global)
{
    delete_all_tables(ets, global);
    synclist_destroy(&ets->ets_tables);
}

Popcorn2EtsStatus popcorn2_ets_create_table(
    term name,
    bool named,
    Popcorn2EtsTableType type,
    Popcorn2EtsTableAccess access,
    size_t key_index,
    term *ret,
    Context *ctx)
{
    assert(ret != NULL);

    if (named) {
        struct Popcorn2EtsTable *table = get_table(
            &ctx->global->popcorn2_ets,
            name,
            ETS_ANY_PROCESS,
            TableAccessNone);

        if (table != NULL) {
            // Don't need to drop lock as we used TableAccessNone
            return Popcorn2EtsTableNameExists;
        }
    }

    struct Popcorn2EtsTable *table = malloc(sizeof(struct Popcorn2EtsTable));
    if (IS_NULL_PTR(table)) {
        return Popcorn2EtsAllocationError;
    }

    EtsMultimapType multimap_type = EtsMultimapTypeSingle;
    if (type == Popcorn2EtsTableBag) {
        multimap_type = EtsMultimapTypeSet;
    } else if (type == Popcorn2EtsTableDuplicateBag) {
        multimap_type = EtsMultimapTypeList;
    }

    EtsMultimap *multimap = ets_multimap_new(multimap_type, key_index);
    if (IS_NULL_PTR(multimap)) {
        free(table);
        return Popcorn2EtsAllocationError;
    }

    list_init(&table->head);

    table->name = name;
    table->named = named;
    table->type = type;
    table->access = access;
    table->key_index = key_index;
    table->owner_process_id = ctx->process_id;
    table->ref_ticks = globalcontext_get_ref_ticks(ctx->global);
    table->multimap = multimap;

#ifndef AVM_NO_SMP
    table->lock = smp_rwlock_create();
#endif

    if (named) {
        *ret = name;
    } else {
        if (UNLIKELY(memory_ensure_free_opt(ctx, REF_SIZE, MEMORY_CAN_SHRINK) != MEMORY_GC_OK)) {
            ets_multimap_delete(multimap, ctx->global);
#ifndef AVM_NO_SMP
            smp_rwlock_destroy(table->lock);
#endif
            free(table);
            return Popcorn2EtsAllocationError;
        }
        *ret = term_from_ref_ticks(table->ref_ticks, &ctx->heap);
    }

    add_table(&ctx->global->popcorn2_ets, table);

    return Popcorn2EtsOk;
}

Popcorn2EtsStatus popcorn2_ets_insert(term name_or_ref, term entry, bool new, Context *ctx)
{
    struct Popcorn2EtsTable *table = get_table(
        &ctx->global->popcorn2_ets,
        name_or_ref,
        ctx->process_id,
        TableAccessWrite);

    if (table == NULL) {
        return Popcorn2EtsBadAccess;
    }

    Popcorn2EtsStatus result = Popcorn2EtsBadEntry;

    if (term_is_tuple(entry)) {
        result = insert_one(table, entry, new, ctx);
    } else if (term_is_list(entry)) {
        result = insert_many(table, entry, new, ctx);
    } else {
        result = Popcorn2EtsBadEntry;
    }

    SMP_UNLOCK(table);

    return result;
}

Popcorn2EtsStatus popcorn2_ets_lookup(term name_or_ref, term key, term *ret, Context *ctx)
{
    assert(ret != NULL);

    struct Popcorn2EtsTable *table = get_table(
        &ctx->global->popcorn2_ets,
        name_or_ref,
        ctx->process_id,
        TableAccessRead);

    if (table == NULL) {
        return Popcorn2EtsBadAccess;
    }

    *ret = term_nil();

    term *tuples = NULL;
    size_t count = 0;

    EtsMultimapStatus result = ets_multimap_lookup(table->multimap, key, &tuples, &count, ctx->global);
    if (UNLIKELY(result == EtsMultimapAllocationError)) {
        SMP_UNLOCK(table);
        return Popcorn2EtsAllocationError;
    }

    if (count == 0) {
        SMP_UNLOCK(table);
        return Popcorn2EtsOk;
    }

    assert(tuples != NULL);

    size_t tuples_size = 0;
    for (size_t i = 0; i < count; i++) {
        tuples_size += memory_estimate_usage(tuples[i]);
    }

    // Terms in `tuples` come from ETS heap, we need to copy them to process heap before returning.
    if (UNLIKELY(memory_ensure_free_opt(ctx, tuples_size + count * CONS_SIZE, MEMORY_CAN_SHRINK) != MEMORY_GC_OK)) {
        free(tuples);
        SMP_UNLOCK(table);
        return Popcorn2EtsAllocationError;
    }

    term list = term_nil();
    for (size_t fwd_i = 0; fwd_i < count; fwd_i++) {
        size_t i = count - fwd_i - 1;
        term tuple = memory_copy_term_tree(&ctx->heap, tuples[i]);
        list = term_list_prepend(tuple, list, &ctx->heap);
    }

    *ret = list;
    free(tuples);

    SMP_UNLOCK(table);
    return Popcorn2EtsOk;
}

Popcorn2EtsStatus popcorn2_ets_delete(term name_or_ref, term key, Context *ctx)
{
    struct Popcorn2EtsTable *table = get_table(
        &ctx->global->popcorn2_ets,
        name_or_ref,
        ctx->process_id,
        TableAccessWrite);

    if (table == NULL) {
        return Popcorn2EtsBadAccess;
    }

    (void) ets_multimap_remove(table->multimap, key, ctx->global);
    SMP_UNLOCK(table);

    return Popcorn2EtsOk;
}

void popcorn2_ets_delete_owned_tables(Popcorn2Ets *ets, int32_t process_id, GlobalContext *global)
{
    struct ListHead *ets_tables = synclist_wrlock(&ets->ets_tables);

    struct ListHead *item, *tmp;
    MUTABLE_LIST_FOR_EACH (item, tmp, ets_tables) {
        struct Popcorn2EtsTable *table = GET_LIST_ENTRY(item, struct Popcorn2EtsTable, head);

        if (table->owner_process_id == process_id) {
            list_remove(&table->head);
            table_destroy(table, global);
        }
    }

    synclist_unlock(&ets->ets_tables);
}

static void table_destroy(struct Popcorn2EtsTable *table, GlobalContext *global)
{
    SMP_WRLOCK(table);
    ets_multimap_delete(table->multimap, global);
    SMP_UNLOCK(table);

#ifndef AVM_NO_SMP
    smp_rwlock_destroy(table->lock);
#endif

    free(table);
}

static void delete_all_tables(Popcorn2Ets *ets, GlobalContext *global)
{
    struct ListHead *ets_tables = synclist_wrlock(&ets->ets_tables);

    struct ListHead *item, *tmp;
    MUTABLE_LIST_FOR_EACH (item, tmp, ets_tables) {
        struct Popcorn2EtsTable *table = GET_LIST_ENTRY(item, struct Popcorn2EtsTable, head);
        list_remove(&table->head);
        table_destroy(table, global);
    }

    synclist_unlock(&ets->ets_tables);
}

static void add_table(Popcorn2Ets *ets, struct Popcorn2EtsTable *table)
{
    struct ListHead *tables = synclist_wrlock(&ets->ets_tables);
    list_append(tables, &table->head);
    synclist_unlock(&ets->ets_tables);
}

static struct Popcorn2EtsTable *get_table(
    Popcorn2Ets *ets,
    term name_or_ref,
    int32_t process_id,
    TableAccess access)
{
    struct ListHead *ets_tables = synclist_rdlock(&ets->ets_tables);
    struct ListHead *item;
    struct Popcorn2EtsTable *ret = NULL;

    uint64_t ref = 0;
    term name = term_invalid_term();
    bool is_atom = term_is_atom(name_or_ref);

    if (is_atom) {
        name = name_or_ref;
    } else {
        ref = term_to_ref_ticks(name_or_ref);
    }

    LIST_FOR_EACH (item, ets_tables) {
        struct Popcorn2EtsTable *table = GET_LIST_ENTRY(item, struct Popcorn2EtsTable, head);
        bool found = is_atom ? table->named && table->name == name : table->ref_ticks == ref;
        if (found) {
            bool is_owner = table->owner_process_id == process_id;
            bool can_read = access == TableAccessRead && (table->access != Popcorn2EtsTableAccessPrivate || is_owner);
            bool can_write = access == TableAccessWrite && (table->access == Popcorn2EtsTableAccessPublic || is_owner);
            bool access_none = access == TableAccessNone;
            if (can_read) {
                SMP_RDLOCK(table);
                ret = table;
            } else if (can_write) {
                SMP_WRLOCK(table);
                ret = table;
            } else if (access_none) {
                ret = table;
            }
            break;
        }
    }

    synclist_unlock(&ets->ets_tables);
    return ret;
}

static Popcorn2EtsStatus insert_one(
    struct Popcorn2EtsTable *table,
    term tuple,
    bool new,
    Context *ctx)
{
    assert(term_is_tuple(tuple));

    EtsMultimapStatus result = EtsMultimapOk;

    if (table->key_index >= (size_t) term_get_tuple_arity(tuple)) {
        return Popcorn2EtsBadEntry;
    }

    if (new) {
        term key = term_get_tuple_element(tuple, table->key_index);
        size_t existing = 0;
        result = ets_multimap_lookup(table->multimap, key, NULL, &existing, ctx->global);
        if (UNLIKELY(result == EtsMultimapAllocationError)) {
            return Popcorn2EtsAllocationError;
        }
        if (existing > 0) {
            return Popcorn2EtsKeyExists;
        }
    }

    result = ets_multimap_insert(table->multimap, &tuple, 1, ctx->global);

    switch (result) {
        case EtsMultimapOk:
            return Popcorn2EtsOk;
        case EtsMultimapAllocationError:
            return Popcorn2EtsAllocationError;
        case EtsMultimapKeyExists:
            return Popcorn2EtsKeyExists;
        default:
            UNREACHABLE();
    }
}

static Popcorn2EtsStatus insert_many(
    struct Popcorn2EtsTable *table,
    term tuples,
    bool new,
    Context *ctx)
{
    assert(term_is_list(tuples));

    EtsMultimapStatus result = EtsMultimapOk;

    size_t count = 0;
    for (term iter = tuples; !term_is_nil(iter); iter = term_get_list_tail(iter), count++) {
        if (!term_is_list(iter)) {
            return Popcorn2EtsBadEntry;  // improper list
        } 

        term tuple = term_get_list_head(iter);

        if (!term_is_tuple(tuple) || table->key_index >= (size_t) term_get_tuple_arity(tuple)) {
            return Popcorn2EtsBadEntry;
        }

        if (new) {
            term key = term_get_tuple_element(tuple, table->key_index);
            size_t existing = 0;
            result = ets_multimap_lookup(table->multimap, key, NULL, &existing, ctx->global);
            if (UNLIKELY(result == EtsMultimapAllocationError)) {
                return Popcorn2EtsAllocationError;
            }
            if (existing > 0) {
                return Popcorn2EtsKeyExists;
            }
        }
    }

    term *to_insert = malloc(sizeof(term) * count);
    if (IS_NULL_PTR(to_insert)) {
        return Popcorn2EtsAllocationError;
    }

    for (size_t i = 0; !term_is_nil(tuples); tuples = term_get_list_tail(tuples), i++) {
        assert(term_is_list(tuples));
        to_insert[i] = term_get_list_head(tuples);
    }

    result = ets_multimap_insert(table->multimap, to_insert, count, ctx->global);

    free(to_insert);

    switch (result) {
        case EtsMultimapOk:
            return Popcorn2EtsOk;
        case EtsMultimapAllocationError:
            return Popcorn2EtsAllocationError;
        case EtsMultimapKeyExists:
            return Popcorn2EtsKeyExists;
        default:
            UNREACHABLE();
    }
}

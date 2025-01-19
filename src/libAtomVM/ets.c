/*
 * This file is part of AtomVM.
 *
 * Copyright 2024 Fred Dushin <fred@dushin.nt>
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

#include "ets.h"

#include "context.h"
#include "defaultatoms.h"
#include "ets_hashtable.h"
#include "list.h"
#include "memory.h"
#include "term.h"
#include "utils.h"
#include <stdint.h>

#define ETS_NO_INDEX SIZE_MAX
#define ETS_ANY_PROCESS -1

#ifndef AVM_NO_SMP
#define SMP_RDLOCK(table) smp_rwlock_rdlock(table->lock)
#define SMP_WRLOCK(table) smp_rwlock_wrlock(table->lock)
#define SMP_UNLOCK(table) smp_rwlock_unlock(table->lock)
#else
#define SMP_RDLOCK(table)
#define SMP_WRLOCK(table)
#define SMP_UNLOCK(table)
#endif

#ifndef AVM_NO_SMP
#ifndef TYPEDEF_RWLOCK
#define TYPEDEF_RWLOCK
typedef struct RWLock RWLock;
#endif
#endif

struct EtsTable
{
    struct ListHead head;
    uint64_t ref_ticks;
    term name;
    bool is_named;
    int32_t owner_process_id;
    size_t key_index;
    EtsTableType table_type;
    // In the future, we might support rb-trees for sorted sets
    // For this MVP, we only support unsorted sets
    struct EtsHashTable *hashtable;
    EtsAccessType access_type;

#ifndef AVM_NO_SMP
    RWLock *lock;
#endif
};
typedef enum TableAccessType
{
    TableAccessNone,
    TableAccessRead,
    TableAccessWrite
} TableAccessType;

static void ets_delete_all_tables(struct Ets *ets, GlobalContext *global);

static void ets_add_table(struct Ets *ets, struct EtsTable *ets_table)
{
    struct ListHead *ets_tables_list = synclist_wrlock(&ets->ets_tables);

    list_append(ets_tables_list, &ets_table->head);

    synclist_unlock(&ets->ets_tables);
}

static struct EtsTable *ets_get_table(struct Ets *ets, int64_t process_id, term name_or_ref, TableAccessType access_type)
{
    struct ListHead *ets_tables_list = synclist_rdlock(&ets->ets_tables);
    struct ListHead *item;
    struct EtsTable *ret = NULL;

    uint64_t ref = 0;
    term name = term_invalid_term();
    bool is_atom = term_is_atom(name_or_ref);
    if (is_atom) {
        name = name_or_ref;
    } else {
        ref = term_to_ref_ticks(name_or_ref);
    }

    LIST_FOR_EACH (item, ets_tables_list) {
        struct EtsTable *table = GET_LIST_ENTRY(item, struct EtsTable, head);
        bool found = is_atom ? table->is_named && table->name == name : table->ref_ticks == ref;
        if (found) {
            bool is_owner = table->owner_process_id == process_id;
            bool can_read = access_type == TableAccessRead && (table->access_type != EtsAccessPrivate || is_owner);
            bool can_write = access_type == TableAccessWrite && (table->access_type == EtsAccessPublic || is_owner);
            bool access_none = access_type == TableAccessNone;
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

void ets_init(struct Ets *ets)
{
    synclist_init(&ets->ets_tables);
}

void ets_destroy(struct Ets *ets, GlobalContext *global)
{
    ets_delete_all_tables(ets, global);
    synclist_destroy(&ets->ets_tables);
}

EtsErrorCode ets_create_table(term name, bool is_named, EtsTableType table_type, EtsAccessType access_type, size_t key_index, term *ret, Context *ctx)
{
    if (is_named) {
        struct EtsTable *ets_table = ets_get_table(&ctx->global->ets, ETS_ANY_PROCESS, name, TableAccessNone);
        if (ets_table != NULL) {
            return EtsTableNameInUse;
        }
    }

    struct EtsTable *ets_table = malloc(sizeof(struct EtsTable));
    if (IS_NULL_PTR(ets_table)) {
        return EtsAllocationFailure;
    }

    list_init(&ets_table->head);

    ets_table->name = name;
    ets_table->is_named = is_named;
    ets_table->access_type = access_type;

    ets_table->table_type = table_type;
    struct EtsHashTable *hashtable = ets_hashtable_new();
    if (IS_NULL_PTR(hashtable)) {
        free(ets_table);
        return EtsAllocationFailure;
    }
    ets_table->hashtable = hashtable;

    ets_table->owner_process_id = ctx->process_id;

    uint64_t ref_ticks = globalcontext_get_ref_ticks(ctx->global);
    ets_table->ref_ticks = ref_ticks;

    ets_table->key_index = key_index;

#ifndef AVM_NO_SMP
    ets_table->lock = smp_rwlock_create();
#endif

    if (is_named) {
        *ret = name;
    } else {
        if (UNLIKELY(memory_ensure_free_opt(ctx, REF_SIZE, MEMORY_CAN_SHRINK) != MEMORY_GC_OK)) {
            ets_hashtable_destroy(hashtable, ctx->global);
            free(ets_table);
            return EtsAllocationFailure;
        }
        *ret = term_from_ref_ticks(ref_ticks, &ctx->heap);
    }

    ets_add_table(&ctx->global->ets, ets_table);

    return EtsOk;
}

static void ets_table_destroy(struct EtsTable *table, GlobalContext *global)
{
    SMP_WRLOCK(table);
    ets_hashtable_destroy(table->hashtable, global);
    SMP_UNLOCK(table);

#ifndef AVM_NO_SMP
    smp_rwlock_destroy(table->lock);
#endif

    free(table);
}

typedef bool (*ets_table_filter_pred)(struct EtsTable *table, void *data);

static void ets_delete_tables_internal(struct Ets *ets, ets_table_filter_pred pred, void *data, GlobalContext *global)
{
    struct ListHead *ets_tables_list = synclist_wrlock(&ets->ets_tables);
    struct ListHead *item;
    struct ListHead *tmp;
    MUTABLE_LIST_FOR_EACH (item, tmp, ets_tables_list) {
        struct EtsTable *table = GET_LIST_ENTRY(item, struct EtsTable, head);
        if (pred(table, data)) {
            list_remove(&table->head);
            ets_table_destroy(table, global);
        }
    }
    synclist_unlock(&ets->ets_tables);
}

static bool equal_process_id_pred(struct EtsTable *table, void *data)
{
    int32_t *process_id = (int32_t *) data;
    return table->owner_process_id == *process_id;
}

void ets_delete_owned_tables(struct Ets *ets, int32_t process_id, GlobalContext *global)
{
    ets_delete_tables_internal(ets, equal_process_id_pred, &process_id, global);
}

static bool true_pred(struct EtsTable *table, void *data)
{
    UNUSED(table);
    UNUSED(data);

    return true;
}

static void ets_delete_all_tables(struct Ets *ets, GlobalContext *global)
{
    ets_delete_tables_internal(ets, true_pred, NULL, global);
}

static bool ets_hashtable_new_heap(size_t size, Heap **new_heap)
{
    Heap *heap = malloc(sizeof(Heap));
    if (IS_NULL_PTR(heap)) {
        return false;
    }

    if (UNLIKELY(memory_init_heap(heap, size) != MEMORY_GC_OK)) {
        free(heap);
        return false;
    }

    *new_heap = heap;
    return true;
}

static EtsErrorCode ets_insert_internal(struct EtsTable *ets_table, term tuple, bool *tuple_inserted, Context *ctx)
{
    size_t arity = (size_t) term_get_tuple_arity(tuple);
    if (ets_table->key_index >= arity) {
        return EtsBadEntry;
    }

    bool is_duplicate_bag = ets_table->table_type == EtsTableDuplicateBag;
    bool insert_new = tuple_inserted != NULL;

    term ets_tuple;
    term ets_key;
    Heap *ets_heap;

    if (is_duplicate_bag) {
        // With duplicate bag mode, we copy entire entries list to new heap fragment.
        // We could create a new heap and merge it with the existing one but we'd need to expose nodes from hashtable.
        // Alternatively, we could use owner's heap, as ets table shouldn't be accessible after owner exited.
        term tuple_key = term_get_tuple_element(tuple, (int) ets_table->key_index);
        term old_tuples = ets_hashtable_lookup(ets_table->hashtable, tuple_key, ctx->global);
        size_t size = memory_estimate_usage(tuple) + memory_estimate_usage(old_tuples) + CONS_SIZE;
        if (UNLIKELY(!ets_hashtable_new_heap(size, &ets_heap))) {
            return EtsAllocationFailure;
        }
        term ets_tuples = memory_copy_term_tree(ets_heap, old_tuples);
        ets_tuple = memory_copy_term_tree(ets_heap, tuple);

        ets_key = term_get_tuple_element(ets_tuple, (int) ets_table->key_index);
        ets_tuple = term_list_prepend(ets_tuple, ets_tuples, ets_heap);
    } else {
        size_t size = memory_estimate_usage(tuple);
        if (!ets_hashtable_new_heap(size, &ets_heap)) {
            return EtsAllocationFailure;
        }

        ets_tuple = memory_copy_term_tree(ets_heap, tuple);
        ets_key = term_get_tuple_element(ets_tuple, (int) ets_table->key_index);
    }

    EtsHashtableOptions opts = insert_new ? 0 : EtsHashtableAllowOverwrite;
    EtsHashtableErrorCode res = ets_hashtable_insert(ets_table->hashtable, ets_key, ets_tuple, opts, ets_heap, ctx->global);
    if (insert_new && res == EtsOk) {
        *tuple_inserted = true;
        return EtsOk;
    } else if (insert_new && res == EtsHashtableKeyAlreadyExists) {
        *tuple_inserted = false;
        memory_destroy_heap(ets_heap, ctx->global);
        return EtsOk;
    } else if (UNLIKELY(res != EtsHashtableOk)) {
        memory_destroy_heap(ets_heap, ctx->global);
        return EtsAllocationFailure;
    }
    return EtsOk;
}

static EtsErrorCode ets_insert_multiple_internal(struct EtsTable *ets_table, term entries, bool *overwritten, Context *ctx)
{
    bool insert_new = overwritten != NULL;
    term iter = entries;
    while (!term_is_nil(iter)) {
        term entry = term_get_list_head(iter);
        bool bad_pos = !term_is_tuple(entry) || ets_table->key_index >= (size_t) term_get_tuple_arity(entry);
        if (bad_pos) {
            return EtsBadEntry;
        }

        if (insert_new) {
            term key = term_get_tuple_element(entry, ets_table->key_index);
            term res = ets_hashtable_lookup(ets_table->hashtable, key, ctx->global);
            bool exists = !term_is_nil(res);
            if (exists) {
                *overwritten = false;
                return EtsOk;
            }
        }

        iter = term_get_list_tail(iter);
    }

    while (term_is_nonempty_list(entries)) {
        term entry = term_get_list_head(entries);
        EtsErrorCode result = ets_insert_internal(ets_table, entry, overwritten, ctx);
        if (UNLIKELY(result != EtsOk)) {
            // Partially inserted list
            // We would need to save previous values (i.e. memory) and reverting can fail (i.e. memory)
            AVM_ABORT();
        }

        entries = term_get_list_tail(entries);
    }

    return EtsOk;
}

EtsErrorCode ets_insert(term ref, term entry, bool *entry_inserted, Context *ctx)
{
    struct EtsTable *ets_table = ets_get_table(&ctx->global->ets, ctx->process_id, ref, TableAccessWrite);
    if (ets_table == NULL) {
        return EtsBadAccess;
    }

    EtsErrorCode result = EtsBadEntry;

    if (term_is_tuple(entry)) {
        result = ets_insert_internal(ets_table, entry, entry_inserted, ctx);
    } else if (term_is_list(entry)) {
        result = ets_insert_multiple_internal(ets_table, entry, entry_inserted, ctx);
    }

    SMP_UNLOCK(ets_table);

    return result;
}

static EtsErrorCode ets_lookup_internal(struct EtsTable *ets_table, term key, size_t index, term *ret, Context *ctx)
{
    bool is_duplicate_bag = ets_table->table_type == EtsTableDuplicateBag;
    bool lookup_element = index != ETS_NO_INDEX;

    term ets_entry = ets_hashtable_lookup(ets_table->hashtable, key, ctx->global);

    if (term_is_nil(ets_entry)) {
        *ret = term_nil();
    } else if (is_duplicate_bag) {
        // for tuple list and it reversed version - we don't want to copy terms in the loop
        size_t size = 2 * memory_estimate_usage(ets_entry);
        // we don't need to preserve tuples, they live on different heap
        if (UNLIKELY(memory_ensure_free_opt(ctx, size, MEMORY_CAN_SHRINK) != MEMORY_GC_OK)) {
            return EtsAllocationFailure;
        }
        term tuples = memory_copy_term_tree(&ctx->heap, ets_entry);
        // lookup returns in insertion order
        // TODO: store it in correct order?
        term reversed = term_nil();
        while (!term_is_nil(tuples)) {
            term tuple = term_get_list_head(tuples);
            if (lookup_element) {
                if (index >= (size_t) term_get_tuple_arity(tuple)) {
                    return EtsBadPosition;
                }
                tuple = term_get_tuple_element(tuple, index);
            }
            reversed = term_list_prepend(tuple, reversed, &ctx->heap);
            tuples = term_get_list_tail(tuples);
        }

        *ret = reversed;
    } else {
        if (lookup_element) {
            if (index >= (size_t) term_get_tuple_arity(ets_entry)) {
                return EtsBadPosition;
            }
            ets_entry = term_get_tuple_element(ets_entry, index);
        }
        size_t size = (size_t) memory_estimate_usage(ets_entry) + CONS_SIZE;
        if (UNLIKELY(memory_ensure_free_opt(ctx, size, MEMORY_CAN_SHRINK) != MEMORY_GC_OK)) {
            return EtsAllocationFailure;
        }
        term tuple = memory_copy_term_tree(&ctx->heap, ets_entry);

        *ret = term_list_prepend(tuple, term_nil(), &ctx->heap);
    }

    return EtsOk;
}

EtsErrorCode ets_lookup(term ref, term key, term *ret, Context *ctx)
{
    struct EtsTable *ets_table = ets_get_table(&ctx->global->ets, ctx->process_id, ref, TableAccessRead);
    if (ets_table == NULL) {
        return EtsBadAccess;
    }

    EtsErrorCode result = ets_lookup_internal(ets_table, key, ETS_NO_INDEX, ret, ctx);
    SMP_UNLOCK(ets_table);

    return result;
}

EtsErrorCode ets_lookup_element(term ref, term key, size_t index, term *ret, Context *ctx)
{
    struct EtsTable *ets_table = ets_get_table(&ctx->global->ets, ctx->process_id, ref, TableAccessRead);
    if (ets_table == NULL) {
        return EtsBadAccess;
    }

    bool is_duplicate_bag = ets_table->table_type == EtsTableDuplicateBag;

    term entry;
    EtsErrorCode result = ets_lookup_internal(ets_table, key, index, &entry, ctx);
    if (result != EtsOk) {
        SMP_UNLOCK(ets_table);
        return result;
    }
    if (term_is_nil(entry)) {
        SMP_UNLOCK(ets_table);
        return EtsEntryNotFound;
    }

    if (is_duplicate_bag) {
        *ret = entry;
    } else {
        *ret = term_get_list_head(entry);
    }
    SMP_UNLOCK(ets_table);

    return EtsOk;
}

EtsErrorCode ets_drop_table(term ref, term *ret, Context *ctx)
{
    struct EtsTable *ets_table = ets_get_table(&ctx->global->ets, ctx->process_id, ref, TableAccessWrite);
    if (ets_table == NULL) {
        return EtsBadAccess;
    }

    synclist_wrlock(&ctx->global->ets.ets_tables);
    SMP_UNLOCK(ets_table);
    list_remove(&ets_table->head);
    ets_table_destroy(ets_table, ctx->global);
    synclist_unlock(&ctx->global->ets.ets_tables);

    *ret = TRUE_ATOM;
    return EtsOk;
}

EtsErrorCode ets_delete(term ref, term key, term *ret, Context *ctx)
{
    struct EtsTable *ets_table = ets_get_table(&ctx->global->ets, ctx->process_id, ref, TableAccessWrite);
    if (ets_table == NULL) {
        return EtsBadAccess;
    }

    bool _found = ets_hashtable_remove(ets_table->hashtable, key, NULL, ctx->global);
    UNUSED(_found);
    SMP_UNLOCK(ets_table);

    *ret = TRUE_ATOM;
    return EtsOk;
}

EtsErrorCode ets_delete_object(term ref, term tuple, term *ret, Context *ctx)
{
    EtsErrorCode error_code = EtsOk;

    struct EtsTable *ets_table = ets_get_table(&ctx->global->ets, ctx->process_id, ref, TableAccessWrite);
    if (ets_table == NULL) {
        error_code = EtsBadAccess;
        goto exit;
    }

    bool is_duplicate_bag = ets_table->table_type == EtsTableDuplicateBag;

    term index = ets_table->key_index;
    if (index >= (size_t) term_get_tuple_arity(tuple)) {
        error_code = EtsBadPosition;
        goto exit;
    }
    term key = term_get_tuple_element(tuple, index);

    if (is_duplicate_bag) {
        term entries = ets_hashtable_lookup(ets_table->hashtable, key, ctx->global);
        if (term_is_nil(entries)) {
            goto exit;
        }

        int proper;
        size_t n = term_list_length(entries, &proper);
        UNUSED(proper);

        term *kept = malloc(n * sizeof(term));
        size_t kept_n = 0;
        while (!term_is_nil(entries)) {
            term entry = term_get_list_head(entries);

            // full element comparison
            TermCompareResult cmp = term_compare(entry, tuple, TermCompareExact, ctx->global);

            bool keep = cmp != TermEquals;
            if (UNLIKELY(cmp == TermCompareMemoryAllocFail)) {
                error_code = EtsAllocationFailure;
                goto exit;
            }
            if (keep) {
                kept[kept_n++] = entry;
            }

            entries = term_get_list_tail(entries);
        }

        bool all_removed = kept_n == 0;
        if (all_removed) {
            bool _found = ets_hashtable_remove(ets_table->hashtable, key, NULL, ctx->global);
            UNUSED(_found);
            free(kept);
        } else {
            size_t memory_needed = memory_estimate_usage(key);
            for (size_t i = 0; i < kept_n; ++i) {
                memory_needed += memory_estimate_usage(kept[i]) + CONS_SIZE;
            }

            Heap *heap;
            if (UNLIKELY(!ets_hashtable_new_heap(memory_needed, &heap))) {
                error_code = EtsAllocationFailure;
                goto exit;
            }

            term filtered = term_nil();
            for (size_t i = 0; i < kept_n; ++i) {
                term copy = memory_copy_term_tree(heap, kept[i]);
                filtered = term_list_prepend(copy, filtered, heap);
            }
            free(kept);

            term new_key = memory_copy_term_tree(heap, key);
            EtsHashtableErrorCode res = ets_hashtable_insert(ets_table->hashtable, new_key, filtered, EtsHashtableAllowOverwrite, heap, ctx->global);
            if (res != EtsHashtableOk) {
                error_code = EtsAllocationFailure;
                goto exit;
            }
        }
    } else {
        term entry = ets_hashtable_lookup(ets_table->hashtable, key, ctx->global);
        if (term_is_nil(entry)) {
            goto exit;
        }
        // full element comparison
        TermCompareResult cmp = term_compare(entry, tuple, TermCompareExact, ctx->global);
        bool remove = cmp == TermEquals;
        if (UNLIKELY(cmp == TermCompareMemoryAllocFail)) {
            error_code = EtsAllocationFailure;
            goto exit;
        }
        if (remove) {
            bool _found = ets_hashtable_remove(ets_table->hashtable, key, NULL, ctx->global);
            UNUSED(_found);
        }
    }

exit:
    *ret = TRUE_ATOM;
    if (LIKELY(ets_table != NULL)) {
        SMP_UNLOCK(ets_table);
    }
    return error_code;
}

static bool operation_to_tuple4(term operation, size_t default_pos, term *position, term *increment, term *threshold, term *set_value)
{
    if (term_is_integer(operation)) {
        *increment = operation;
        *position = term_from_int(default_pos);
        *threshold = term_invalid_term();
        *set_value = term_invalid_term();
        return true;
    }

    if (!term_is_tuple(operation)) {
        return false;
    }
    int n = term_get_tuple_arity(operation);
    if (n != 2 && n != 4) {
        return false;
    }

    term pos = term_get_tuple_element(operation, 0);
    term incr = term_get_tuple_element(operation, 1);
    if (!term_is_integer(pos) || !term_is_integer(incr)) {
        return false;
    }

    if (n == 2) {
        *position = pos;
        *increment = incr;
        *threshold = term_invalid_term();
        *set_value = term_invalid_term();
        return true;
    }

    term tresh = term_get_tuple_element(operation, 2);
    term set_val = term_get_tuple_element(operation, 3);
    if (!term_is_integer(tresh) || !term_is_integer(set_val)) {
        return false;
    }

    *position = pos;
    *increment = incr;
    *threshold = tresh;
    *set_value = set_val;
    return true;
}

EtsErrorCode ets_update_counter(term ref, term key, term operation, term default_value, term *ret, Context *ctx)
{
    struct EtsTable *ets_table = ets_get_table(&ctx->global->ets, ctx->process_id, ref, TableAccessWrite);
    if (ets_table == NULL) {
        return EtsBadAccess;
    }

    bool is_duplicate_bag = ets_table->table_type == EtsTableDuplicateBag;
    if (is_duplicate_bag) {
        SMP_UNLOCK(ets_table);
        return EtsBadAccess;
    }

    term to_insert = term_invalid_term();
    term list = term_invalid_term();
    EtsErrorCode result = ets_lookup_internal(ets_table, key, ETS_NO_INDEX, &list, ctx);
    if (result != EtsOk) {
        SMP_UNLOCK(ets_table);
        return result;
    }
    if (term_is_nil(list)) {
        if (term_is_invalid_term(default_value)) {
            SMP_UNLOCK(ets_table);
            return EtsBadEntry;
        }
        to_insert = default_value;
    } else {
        to_insert = term_get_list_head(list);
    }

    if (!(term_is_tuple(to_insert))) {
        SMP_UNLOCK(ets_table);
        return EtsBadEntry;
    }
    term position_term, increment_term, threshold_term, set_value_term;

    // +1 to position, +1 to elem after key
    size_t default_pos = (ets_table->key_index + 1) + 1;
    if (!operation_to_tuple4(operation, default_pos, &position_term, &increment_term, &threshold_term, &set_value_term)) {
        SMP_UNLOCK(ets_table);
        return EtsBadEntry;
    }
    int arity = term_get_tuple_arity(to_insert);
    avm_int_t position = term_to_int(position_term);
    if (position < 0) {
        SMP_UNLOCK(ets_table);
        return EtsBadEntry;
    }
    size_t index = position - 1;
    if (index >= (size_t) arity) {
        SMP_UNLOCK(ets_table);
        return EtsBadEntry;
    }

    term elem = term_get_tuple_element(to_insert, index);
    if (!term_is_integer(elem)) {
        SMP_UNLOCK(ets_table);
        return EtsBadEntry;
    }
    int increment = term_to_int(increment_term);
    // We don't check overflow here.
    int elem_value = term_to_int(elem) + increment;
    if (!term_is_invalid_term(threshold_term) && !term_is_invalid_term(set_value_term)) {
        int threshold = term_to_int(threshold_term);
        int set_value = term_to_int(set_value_term);

        if (increment >= 0 && elem_value > threshold) {
            elem_value = set_value;
        } else if (increment < 0 && elem_value < threshold) {
            elem_value = set_value;
        }
    }

    elem = term_from_int(elem_value);
    term_put_tuple_element(to_insert, index, elem);
    EtsErrorCode insert_result = ets_insert_internal(ets_table, to_insert, NULL, ctx);
    if (insert_result == EtsOk) {
        *ret = elem;
    }
    SMP_UNLOCK(ets_table);
    return insert_result;
}

EtsErrorCode ets_update_element(term ref, term key, term value, size_t index, term *ret, Context *ctx)
{
    struct EtsTable *ets_table = ets_get_table(&ctx->global->ets, ctx->process_id, ref, TableAccessWrite);
    if (ets_table == NULL) {
        return EtsBadAccess;
    }

    bool is_duplicate_bag = ets_table->table_type == EtsTableDuplicateBag;
    if (is_duplicate_bag) {
        SMP_UNLOCK(ets_table);
        return EtsBadAccess;
    }
    term to_insert = term_invalid_term();
    term list = term_invalid_term();
    EtsErrorCode result = ets_lookup_internal(ets_table, key, ETS_NO_INDEX, &list, ctx);
    if (result != EtsOk) {
        SMP_UNLOCK(ets_table);
        return result;
    }
    if (term_is_nil(list)) {
        SMP_UNLOCK(ets_table);
        *ret = FALSE_ATOM;
        return EtsOk;
    }

    to_insert = term_get_list_head(list);

    if (!(term_is_tuple(to_insert))) {
        SMP_UNLOCK(ets_table);
        return EtsBadEntry;
    }

    int arity = term_get_tuple_arity(to_insert);
    if (index >= (size_t) arity) {
        SMP_UNLOCK(ets_table);
        return EtsBadEntry;
    }

    term_put_tuple_element(to_insert, index, value);
    EtsErrorCode insert_result = ets_insert_internal(ets_table, to_insert, NULL, ctx);
    SMP_UNLOCK(ets_table);
    *ret = TRUE_ATOM;
    return insert_result;
}

EtsErrorCode ets_take(term ref, term key, term *ret, Context *ctx)
{
    struct EtsTable *ets_table = ets_get_table(&ctx->global->ets, ctx->process_id, ref, TableAccessWrite);
    if (ets_table == NULL) {
        return EtsBadAccess;
    }

    struct EtsHashTableEntry deleted;
    bool found = ets_hashtable_remove(ets_table->hashtable, key, &deleted, ctx->global);
    // we can unlock here because hashtable doesn't have reference to the entry and its heap anymore
    SMP_UNLOCK(ets_table);

    if (!found) {
        *ret = term_nil();
        return EtsOk;
    }

    bool is_duplicate_bag = ets_table->table_type == EtsTableDuplicateBag;
    size_t size = 0;
    if (is_duplicate_bag) {
        size = 2 * memory_estimate_usage(deleted.entry);
    } else {
        size = memory_estimate_usage(deleted.entry) + CONS_SIZE;
    }
    // we don't need to preserve tuples, they live on different heap
    if (UNLIKELY(memory_ensure_free_opt(ctx, size, MEMORY_CAN_SHRINK) != MEMORY_GC_OK)) {
        return EtsAllocationFailure;
    }
    term entry_or_entries = memory_copy_term_tree(&ctx->heap, deleted.entry);
    memory_destroy_heap(deleted.heap, ctx->global);

    if (is_duplicate_bag) {
        term taken = term_nil();

        // return in insertion order
        while (!term_is_nil(entry_or_entries)) {
            term entry = term_get_list_head(entry_or_entries);
            taken = term_list_prepend(entry, taken, &ctx->heap);
            entry_or_entries = term_get_list_tail(entry_or_entries);
        }
        *ret = taken;
    } else {
        *ret = term_list_prepend(entry_or_entries, term_nil(), &ctx->heap);
    }

    return EtsOk;
}

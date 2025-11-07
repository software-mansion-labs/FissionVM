// See popcorn_nifs.gperf for status of each NIF

#include "popcorn_nifs.h"

#include "popcorn_ets.h"

#include "nifs.h"

#include "atom_table.h"
#include "avmpack.h"
#include "bif.h"
#include "bitstring.h"
#include "context.h"
#include "defaultatoms.h"
#include "dictionary.h"
#include "dist_nifs.h"
#include "erl_nif_priv.h"
#include "externalterm.h"
#include "globalcontext.h"
#include "interop.h"
#include "mailbox.h"
#include "memory.h"
#include "module.h"
#include "platform_nifs.h"
#include "port.h"
#include "posix_nifs.h"
#include "scheduler.h"
#include "synclist.h"
#include "sys.h"
#include "term.h"
#include "term_typedef.h"
#include "unicode.h"
#include "utils.h"

#include <errno.h>
#include <fenv.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static term nif_code_get_object_code(Context *ctx, int argc, term argv[])
{
    UNUSED(ctx);
    UNUSED(argc);
    UNUSED(argv);
    return ERROR_ATOM;
}

static const struct Nif code_get_object_code_nif = {
    .base.type = NIFFunctionType,
    .nif_ptr = nif_code_get_object_code
};

static void get_pattern_data_with_sizes(term pattern_term, const char **pattern_data, size_t *sizes, size_t *shortest_pattern_length)
{
    if (term_is_binary(pattern_term)) {
        pattern_data[0] = term_binary_data(pattern_term);
        sizes[0] = term_binary_size(pattern_term);
        *shortest_pattern_length = sizes[0];
    }

    for (size_t i = 0; term_is_nonempty_list(pattern_term); ++i) {
        term head = term_get_list_head(pattern_term);
        pattern_data[i] = term_binary_data(head);
        sizes[i] = term_binary_size(head);
        if (i == 0 || sizes[i] < *shortest_pattern_length) {
            *shortest_pattern_length = sizes[i];
        }
        pattern_term = term_get_list_tail(pattern_term);
    }
}

static const char *find_pattern(const char *bin, size_t bin_size, const char **patterns, const size_t *pattern_sizes, size_t patterns_len, int *matched_pattern_index)
{
    for (size_t i = 0; i < bin_size; i++) {
        for (size_t pattern_i = 0; pattern_i < patterns_len; pattern_i++) {
            if (pattern_sizes[pattern_i] <= bin_size - i) {
                if (memcmp(bin + i, patterns[pattern_i], pattern_sizes[pattern_i]) == 0) {
                    *matched_pattern_index = pattern_i;
                    return bin + i;
                }
            }
        }
    }
    return NULL;
}

term trim_list(Context *ctx, term list, size_t heap_size, bool trim, bool trim_all)
{
    int proper;
    size_t length = term_list_length(list, &proper);
    UNUSED(proper);
    term *cons = malloc(length * sizeof(term));
    if (IS_NULL_PTR(cons)) {
        RAISE_ERROR(OUT_OF_MEMORY_ATOM);
    }

    if (UNLIKELY(memory_ensure_free_with_roots(ctx, heap_size, 1, &list, MEMORY_CAN_SHRINK) != MEMORY_GC_OK)) {
        free(cons);
        RAISE_ERROR(OUT_OF_MEMORY_ATOM);
    }

    term iter = list;

    for (size_t i = 0; i < length; ++i) {
        cons[i] = iter;
        iter = term_get_list_tail(iter);
    }

    bool found_non_empty = false;
    term trimmed = term_nil();
    for (long long i = length - 1; i >= 0; --i) {
        term head = term_get_list_head(cons[i]);

        bool is_empty = term_binary_size(head) == 0;
        if (is_empty) {
            bool trim_tail = trim && !found_non_empty;
            if (!trim_tail && !trim_all) {
                trimmed = term_list_prepend(head, trimmed, &ctx->heap);
            }
        } else {
            trimmed = term_list_prepend(head, trimmed, &ctx->heap);
            found_non_empty = true;
        }
    }

    free(cons);
    return trimmed;
}

static term nif_binary_split(Context *ctx, int argc, term argv[])
{
    term bin_term = argv[0];
    term pattern_term = argv[1];

    VALIDATE_VALUE(bin_term, term_is_binary);
    if (!term_is_binary(pattern_term) && !term_is_nonempty_list(pattern_term)) {
        RAISE_ERROR(BADARG_ATOM);
    }
    bool global = false;
    bool trim = false;
    bool trim_all = false;

    if (argc == 3) {
        term options = argv[2];
        if (UNLIKELY(!term_is_list(options))) {
            RAISE_ERROR(BADARG_ATOM);
        }
        term head;
        term tail = options;
        while (term_is_nonempty_list(tail)) {
            head = term_get_list_head(tail);
            tail = term_get_list_tail(tail);
            switch (head) {
                case GLOBAL_ATOM:
                    global = true;
                    break;
                case TRIM_ATOM:
                    trim = true;
                    break;
                case TRIM_ALL_ATOM:
                    trim_all = true;
                    break;
                default:
                    RAISE_ERROR(BADARG_ATOM);
            }
        }
    }
    size_t pattern_list_size = 1;
    if (term_is_list(pattern_term)) {
        int proper;
        pattern_list_size = term_list_length(pattern_term, &proper);
        if (UNLIKELY(!proper)) {
            RAISE_ERROR(BADARG_ATOM);
        }
        term iter = pattern_term;
        while (term_is_nonempty_list(iter)) {
            term head = term_get_list_head(iter);
            if (UNLIKELY(term_binary_size(head) == 0)) {
                RAISE_ERROR(BADARG_ATOM);
            }
            iter = term_get_list_tail(iter);
        }
    } else if (term_is_binary(pattern_term)) {
        size_t pattern_size = term_binary_size(pattern_term);
        if (UNLIKELY(pattern_size == 0)) {
            RAISE_ERROR(BADARG_ATOM);
        }
    }

    int bin_size = term_binary_size(bin_term);

    const char *bin_data = term_binary_data(bin_term);
    const char **pattern_data = malloc(sizeof(char *) * pattern_list_size);
    if (IS_NULL_PTR(pattern_data)) {
        RAISE_ERROR(OUT_OF_MEMORY_ATOM);
    }

    size_t *sizes = malloc(sizeof(size_t) * pattern_list_size);
    if (IS_NULL_PTR(sizes)) {
        free(pattern_data);
        RAISE_ERROR(OUT_OF_MEMORY_ATOM);
    }
    size_t shortest_pattern_length;

    get_pattern_data_with_sizes(pattern_term, pattern_data, sizes, &shortest_pattern_length);

    // Count segments first to allocate memory once.
    size_t num_segments = 1;
    const char *temp_bin_data = bin_data;
    size_t temp_bin_size = bin_size;
    size_t heap_size = 0;
    do {
        int matched_pattern_index;
        const char *found = find_pattern(temp_bin_data, temp_bin_size, pattern_data, sizes, pattern_list_size, &matched_pattern_index);
        if (!found) {
            break;
        }
        num_segments++;
        heap_size += CONS_SIZE + term_sub_binary_heap_size(argv[0], found - temp_bin_data);
        int next_search_offset = found - temp_bin_data + sizes[matched_pattern_index];
        temp_bin_data += next_search_offset;
        temp_bin_size -= next_search_offset;
    } while (global && temp_bin_size >= shortest_pattern_length);

    heap_size += CONS_SIZE + term_sub_binary_heap_size(argv[0], temp_bin_size);

    term result_list = term_nil();

    if (num_segments == 1) {
        // not found
        if (UNLIKELY(memory_ensure_free_with_roots(ctx, 2, 1, argv, MEMORY_CAN_SHRINK) != MEMORY_GC_OK)) {
            free(pattern_data);
            free(sizes);
            RAISE_ERROR(OUT_OF_MEMORY_ATOM);
        }

        return term_list_prepend(argv[0], result_list, &ctx->heap);
    }

    // binary:split/2,3 always return sub binaries, except when copied binaries are as small as sub-binaries.
    if (UNLIKELY(memory_ensure_free_with_roots(ctx, heap_size, 2, argv, MEMORY_CAN_SHRINK) != MEMORY_GC_OK)) {
        free(pattern_data);
        free(sizes);
        RAISE_ERROR(OUT_OF_MEMORY_ATOM);
    }

    // Allocate list first
    for (size_t index_segments = 0; index_segments < num_segments; index_segments++) {
        result_list = term_list_prepend(term_nil(), result_list, &ctx->heap);
    }

    // Reset pointers after allocation
    bin_data = term_binary_data(argv[0]);

    pattern_term = argv[1];
    get_pattern_data_with_sizes(pattern_term, pattern_data, sizes, &shortest_pattern_length);

    term list_cursor = result_list;
    temp_bin_data = bin_data;
    temp_bin_size = bin_size;
    term *list_ptr = term_get_list_ptr(list_cursor);
    do {
        int matched_pattern_index;
        const char *found = find_pattern(temp_bin_data, temp_bin_size, pattern_data, sizes, pattern_list_size, &matched_pattern_index);

        if (found) {
            term tok = term_maybe_create_sub_binary(argv[0], temp_bin_data - bin_data, found - temp_bin_data, &ctx->heap, ctx->global);
            list_ptr[LIST_HEAD_INDEX] = tok;

            list_cursor = list_ptr[LIST_TAIL_INDEX];
            list_ptr = term_get_list_ptr(list_cursor);

            int next_search_offset = found - temp_bin_data + sizes[matched_pattern_index];
            temp_bin_data += next_search_offset;
            temp_bin_size -= next_search_offset;
        }

        if (!found || !global) {
            term rest = term_maybe_create_sub_binary(argv[0], temp_bin_data - bin_data, temp_bin_size, &ctx->heap, ctx->global);
            list_ptr[LIST_HEAD_INDEX] = rest;
            break;
        }
    } while (!term_is_nil(list_cursor));

    free(pattern_data);
    free(sizes);

    if (trim || trim_all) {
        result_list = trim_list(ctx, result_list, heap_size, trim, trim_all);
    }

    return result_list;
}

static const struct Nif binary_split_nif = {
    .base.type = NIFFunctionType,
    .nif_ptr = nif_binary_split
};

static term nif_erlang_display_string_2(Context *ctx, int argc, term argv[])
{
    UNUSED(ctx);
    UNUSED(argc);

    FILE *fd;
    if (argv[0] == STDOUT_ATOM) {
        fd = stdout;
    } else if (argv[0] == STDERR_ATOM) {
        fd = stderr;
    } else {
        RAISE_ERROR(BADARG_ATOM);
    }

    term t = argv[1];
    if (term_is_nonempty_list(t)) {
        int ok;
        char *printable = interop_list_to_string(t, &ok);
        if (UNLIKELY(!ok)) {
            RAISE_ERROR(BADARG_ATOM);
        }

        fputs(printable, fd);
        free(printable);
    } else if (term_is_binary(t)) {
        size_t len = term_binary_size(t);
        const char *binary_data = term_binary_data(t);
        fwrite(binary_data, sizeof(*binary_data), len, fd);
    } else if (term_is_nil(t)) {
        return TRUE_ATOM;
    } else {
        RAISE_ERROR(BADARG_ATOM);
    }

    return TRUE_ATOM;
}

static const struct Nif display_string_nif = {
    .base.type = NIFFunctionType,
    .nif_ptr = nif_erlang_display_string_2
};

// The difference from regular term comparison is that this function
// always compares term type first, which in practice means that
// an integer is always lesser than a float.
static term nif_erts_internal_cmp_term(Context *ctx, int argc, term argv[])
{
    UNUSED(argc);
    TermCompareResult result = term_compare(argv[0], argv[1], TermCompareExact, ctx->global);
    switch (result) {
        case TermEquals:
            return term_from_int4(0);
        case TermGreaterThan:
            return term_from_int4(1);
        case TermLessThan:
            return term_from_int4(-1);
        case TermCompareMemoryAllocFail:
            RAISE_ERROR(OUT_OF_MEMORY_ATOM);
        default:
            AVM_ABORT();
    }
}

static const struct Nif erts_internal_cmp_term_nif = {
    .base.type = NIFFunctionType,
    .nif_ptr = nif_erts_internal_cmp_term
};

// ETS

static term nif_ets_new(Context *ctx, int argc, term argv[])
{
    UNUSED(argc);

    term name = argv[0];
    VALIDATE_VALUE(name, term_is_atom);

    term options = argv[1];
    VALIDATE_VALUE(options, term_is_list);

    term is_named = interop_kv_get_value_default(options, ATOM_STR("\xB", "named_table"), FALSE_ATOM, ctx->global);
    term keypos = interop_kv_get_value_default(options, ATOM_STR("\x6", "keypos"), term_from_int(1), ctx->global);
    avm_int_t index = term_to_int(keypos) - 1;

    if (UNLIKELY(index < 0)) {
        RAISE_ERROR(BADARG_ATOM);
    }

    term private = interop_kv_get_value(options, ATOM_STR("\x7", "private"), ctx->global);
    term public = interop_kv_get_value(options, ATOM_STR("\x6", "public"), ctx->global);

    PopcornEtsAccessType access = PopcornEtsAccessProtected;
    if (!term_is_invalid_term(private)) {
        access = PopcornEtsAccessPrivate;
    } else if (!term_is_invalid_term(public)) {
        access = PopcornEtsAccessPublic;
    }

    PopcornEtsTableType type = PopcornEtsTableSet;
    term is_duplicate_bag = interop_kv_get_value_default(options, ATOM_STR("\xd", "duplicate_bag"), FALSE_ATOM, ctx->global) == TRUE_ATOM;
    if (is_duplicate_bag) {
        type = PopcornEtsTableDuplicateBag;
    }

    term table = term_invalid_term();
    PopcornEtsErrorCode result = popcorn_ets_create_table(name, is_named == TRUE_ATOM, type, access, (size_t) index, &table, ctx);
    switch (result) {
        case PopcornEtsOk:
            return table;
        case PopcornEtsTableNameInUse:
            RAISE_ERROR(BADARG_ATOM);
        case PopcornEtsAllocationFailure:
            RAISE_ERROR(MEMORY_ATOM);
        default:
            AVM_ABORT();
    }
}

static inline bool is_popcorn_ets_table_id(term t)
{
    return term_is_reference(t) || term_is_atom(t);
}

static term nif_ets_insert(Context *ctx, int argc, term argv[])
{
    UNUSED(argc);

    term ref = argv[0];
    VALIDATE_VALUE(ref, is_popcorn_ets_table_id);

    term entry = argv[1];

    PopcornEtsErrorCode result = popcorn_ets_insert(ref, entry, NULL, ctx);
    switch (result) {
        case PopcornEtsOk:
            return TRUE_ATOM;
        case PopcornEtsBadAccess:
        case PopcornEtsBadEntry:
            RAISE_ERROR(BADARG_ATOM);
        case PopcornEtsAllocationFailure:
            RAISE_ERROR(MEMORY_ATOM);
        default:
            AVM_ABORT();
    }
}

static term nif_ets_insert_new(Context *ctx, int argc, term argv[])
{
    UNUSED(argc);

    term ref = argv[0];
    VALIDATE_VALUE(ref, is_popcorn_ets_table_id);
    term to_insert = argv[1];
    bool entry_inserted = false;

    PopcornEtsErrorCode result = popcorn_ets_insert(ref, to_insert, &entry_inserted, ctx);
    switch (result) {
        case PopcornEtsOk:
            return entry_inserted ? TRUE_ATOM : FALSE_ATOM;
        case PopcornEtsBadAccess:
        case PopcornEtsBadEntry:
            RAISE_ERROR(BADARG_ATOM);
        case PopcornEtsAllocationFailure:
            RAISE_ERROR(MEMORY_ATOM);
        default:
            AVM_ABORT();
    }
}

static term nif_ets_lookup(Context *ctx, int argc, term argv[])
{
    UNUSED(argc);

    term ref = argv[0];
    VALIDATE_VALUE(ref, is_popcorn_ets_table_id);

    term key = argv[1];

    term ret = term_invalid_term();
    PopcornEtsErrorCode result = popcorn_ets_lookup(ref, key, &ret, ctx);
    switch (result) {
        case PopcornEtsOk:
            return ret;
        case PopcornEtsBadAccess:
        case PopcornEtsBadPosition:
            RAISE_ERROR(BADARG_ATOM);
        case PopcornEtsAllocationFailure:
            RAISE_ERROR(MEMORY_ATOM);
        default:
            AVM_ABORT();
    }
}

static term nif_ets_member(Context *ctx, int argc, term argv[])
{
    UNUSED(argc);

    term ref = argv[0];
    VALIDATE_VALUE(ref, is_popcorn_ets_table_id);

    term key = argv[1];

    term ret = term_invalid_term();
    PopcornEtsErrorCode result = popcorn_ets_lookup(ref, key, &ret, ctx);
    switch (result) {
        case PopcornEtsOk:
            return term_is_nil(ret) ? FALSE_ATOM : TRUE_ATOM;
        case PopcornEtsBadAccess:
            RAISE_ERROR(BADARG_ATOM);
        case PopcornEtsAllocationFailure:
            RAISE_ERROR(MEMORY_ATOM);
        default:
            AVM_ABORT();
    }
}

static term nif_ets_take(Context *ctx, int argc, term argv[])
{
    UNUSED(argc);

    term ref = argv[0];
    VALIDATE_VALUE(ref, is_popcorn_ets_table_id);

    term key = argv[1];

    term ret = term_invalid_term();
    PopcornEtsErrorCode result = popcorn_ets_take(ref, key, &ret, ctx);
    switch (result) {
        case PopcornEtsOk:
            return ret;
        case PopcornEtsBadAccess:
            RAISE_ERROR(BADARG_ATOM);
        case PopcornEtsAllocationFailure:
            RAISE_ERROR(MEMORY_ATOM);
        default:
            AVM_ABORT();
    }
}

static term nif_ets_update_counter(Context *ctx, int argc, term argv[])
{
    term ref = argv[0];
    VALIDATE_VALUE(ref, is_popcorn_ets_table_id);

    term key = argv[1];
    term operation = argv[2];
    term default_value = term_invalid_term();
    if (argc == 4) {
        default_value = argv[3];
        VALIDATE_VALUE(default_value, term_is_tuple);
        term_put_tuple_element(default_value, 0, key);
    }
    term ret = term_invalid_term();
    PopcornEtsErrorCode result = popcorn_ets_update_counter(ref, key, operation, default_value, &ret, ctx);
    switch (result) {
        case PopcornEtsOk:
            return ret;
        case PopcornEtsBadAccess:
        case PopcornEtsBadEntry:
            RAISE_ERROR(BADARG_ATOM);
        case PopcornEtsAllocationFailure:
            RAISE_ERROR(MEMORY_ATOM);
        default:
            AVM_ABORT();
    }
}

static term nif_ets_update_element(Context *ctx, int argc, term argv[])
{
    UNUSED(argc);
    term ref = argv[0];
    VALIDATE_VALUE(ref, is_popcorn_ets_table_id);

    term key = argv[1];
    term operation = argv[2];
    VALIDATE_VALUE(operation, term_is_tuple);
    if (term_get_tuple_arity(operation) != 2) {
        RAISE_ERROR(BADARG_ATOM);
    }
    term pos = term_get_tuple_element(operation, 0);
    VALIDATE_VALUE(pos, term_is_integer);
    term value = term_get_tuple_element(operation, 1);

    avm_int_t index = term_to_int(pos) - 1;
    if (UNLIKELY(index < 0)) {
        RAISE_ERROR(BADARG_ATOM);
    }

    term ret = term_invalid_term();
    PopcornEtsErrorCode result = popcorn_ets_update_element(ref, key, value, (size_t) index, &ret, ctx);
    switch (result) {
        case PopcornEtsOk:
            return ret;
        case PopcornEtsBadAccess:
        case PopcornEtsBadEntry:
            RAISE_ERROR(BADARG_ATOM);
        case PopcornEtsAllocationFailure:
            RAISE_ERROR(MEMORY_ATOM);
        default:
            AVM_ABORT();
    }
}

static term nif_ets_lookup_element(Context *ctx, int argc, term argv[])
{
    term ref = argv[0];
    VALIDATE_VALUE(ref, is_popcorn_ets_table_id);

    term key = argv[1];
    term pos = argv[2];
    VALIDATE_VALUE(pos, term_is_integer);
    avm_int_t index = term_to_int(pos) - 1;
    if (UNLIKELY(index < 0)) {
        RAISE_ERROR(BADARG_ATOM);
    }

    term default_value = term_invalid_term();
    if (argc == 4) {
        default_value = argv[3];
    }

    term ret = term_invalid_term();
    PopcornEtsErrorCode result = popcorn_ets_lookup_element(ref, key, (size_t) index, &ret, ctx);
    switch (result) {
        case PopcornEtsOk:
            return ret;
        case PopcornEtsEntryNotFound:
            if (!term_is_invalid_term(default_value)) {
                return default_value;
            }
        case PopcornEtsBadPosition:
        case PopcornEtsBadAccess:
            RAISE_ERROR(BADARG_ATOM);
        case PopcornEtsAllocationFailure:
            RAISE_ERROR(MEMORY_ATOM);
        default:
            AVM_ABORT();
    }
}

static term nif_ets_delete(Context *ctx, int argc, term argv[])
{
    term ref = argv[0];
    VALIDATE_VALUE(ref, is_popcorn_ets_table_id);
    term ret = term_invalid_term();
    PopcornEtsErrorCode result;
    if (argc == 2) {
        term key = argv[1];
        result = popcorn_ets_delete(ref, key, &ret, ctx);
    } else {
        result = popcorn_ets_drop_table(ref, &ret, ctx);
    }

    switch (result) {
        case PopcornEtsOk:
            return ret;
        case PopcornEtsBadAccess:
            RAISE_ERROR(BADARG_ATOM);
        case PopcornEtsAllocationFailure:
            RAISE_ERROR(MEMORY_ATOM);
        default:
            AVM_ABORT();
    }
}

static term nif_ets_delete_object(Context *ctx, int argc, term argv[])
{
    UNUSED(argc);

    term ref = argv[0];
    VALIDATE_VALUE(ref, is_popcorn_ets_table_id);

    term tuple = argv[1];
    VALIDATE_VALUE(tuple, term_is_tuple);

    term ret = term_invalid_term();
    PopcornEtsErrorCode result = popcorn_ets_delete_object(ref, tuple, &ret, ctx);
    switch (result) {
        case PopcornEtsOk:
            return ret;
        case PopcornEtsBadAccess:
        case PopcornEtsBadPosition:
            RAISE_ERROR(BADARG_ATOM);
        case PopcornEtsAllocationFailure:
            RAISE_ERROR(MEMORY_ATOM);
        default:
            AVM_ABORT();
    }
}

static const struct Nif ets_new_nif = {
    .base.type = NIFFunctionType,
    .nif_ptr = nif_ets_new
};

static const struct Nif ets_insert_nif = {
    .base.type = NIFFunctionType,
    .nif_ptr = nif_ets_insert
};

static const struct Nif ets_insert_new_nif = {
    .base.type = NIFFunctionType,
    .nif_ptr = nif_ets_insert_new
};

static const struct Nif ets_update_counter_nif = {
    .base.type = NIFFunctionType,
    .nif_ptr = nif_ets_update_counter
};

static const struct Nif ets_update_element_nif = {
    .base.type = NIFFunctionType,
    .nif_ptr = nif_ets_update_element
};

static const struct Nif ets_lookup_nif = {
    .base.type = NIFFunctionType,
    .nif_ptr = nif_ets_lookup
};

static const struct Nif ets_member_nif = {
    .base.type = NIFFunctionType,
    .nif_ptr = nif_ets_member
};

static const struct Nif ets_take_nif = {
    .base.type = NIFFunctionType,
    .nif_ptr = nif_ets_take
};

static const struct Nif ets_lookup_element_nif = {
    .base.type = NIFFunctionType,
    .nif_ptr = nif_ets_lookup_element
};

static const struct Nif ets_delete_nif = {
    .base.type = NIFFunctionType,
    .nif_ptr = nif_ets_delete
};

static const struct Nif ets_delete_object_nif = {
    .base.type = NIFFunctionType,
    .nif_ptr = nif_ets_delete_object
};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "popcorn_nifs_hash.h"
#pragma GCC diagnostic pop

const struct Nif *popcorn_nifs_get_nif(const char *mfa)
{
    const NifNameAndNifPtr *nameAndPtr = popcorn_nif_in_word_set(mfa, strlen(mfa));
    if (nameAndPtr) {
        return nameAndPtr->nif;
    }
    return NULL;
}

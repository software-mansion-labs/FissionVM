// See popcorn_nifs.gperf for status of each NIF

#include "popcorn_nifs.h"
#include "popcorn_ets.h"
#include "popcorn_md5.h"

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

static term iolist_to_buffer(term list, char **buf, size_t *size)
{
    *buf = NULL;
    *size = 0;

    size_t bin_size;
    switch (interop_iolist_size(list, &bin_size)) {
        case InteropOk:
            break;
        case InteropMemoryAllocFail:
            return OUT_OF_MEMORY_ATOM;
        case InteropBadArg:
            return BADARG_ATOM;
    }

    if (bin_size == 0) {
        return OK_ATOM;
    }

    char *bin_buf = NULL;
    bin_buf = malloc(bin_size * sizeof(char));
    if (IS_NULL_PTR(bin_buf)) {
        return OUT_OF_MEMORY_ATOM;
    }

    switch (interop_write_iolist(list, bin_buf)) {
        case InteropOk:
            break;
        case InteropMemoryAllocFail:
            free(bin_buf);
            return OUT_OF_MEMORY_ATOM;
        case InteropBadArg:
            free(bin_buf);
            return BADARG_ATOM;
    }

    *buf = bin_buf;
    *size = bin_size;
    return OK_ATOM;
}

static term nif_erlang_list_to_bitstring_1(Context *ctx, int argc, term argv[])
{
    // TODO: this is a copy-pasted erlang_list_to_bitstring
    // we shouldimplement proper list_to_bitstring function when the bitstrings are supported
    UNUSED(argc);

    term t = argv[0];
    VALIDATE_VALUE(t, term_is_list);

    char *bin_buf = NULL;
    char *alloc_ptr = NULL;
    size_t bin_size = 0;

    term status = iolist_to_buffer(t, &bin_buf, &bin_size);
    if (UNLIKELY(status != OK_ATOM)) {
        RAISE_ERROR(status);
    }
    bool allocated = bin_size > 0;
    if (allocated) {
        alloc_ptr = bin_buf;
    } else {
        bin_buf = "";
        bin_size = 0;
    }

    if (UNLIKELY(memory_ensure_free(ctx, term_binary_heap_size(bin_size)) != MEMORY_GC_OK)) {
        free(alloc_ptr);
        RAISE_ERROR(OUT_OF_MEMORY_ATOM);
    }
    term bin_res = term_from_literal_binary(bin_buf, bin_size, &ctx->heap, ctx->global);

    free(alloc_ptr);
    return bin_res;
}

static const struct Nif list_to_bitstring_nif = {
    .base.type = NIFFunctionType,
    .nif_ptr = nif_erlang_list_to_bitstring_1
};

static InteropFunctionResult md5_hash_vendored_fold_fun(term t, void *accum)
{
    MD5Context *ctx = (MD5Context *) accum;
    if (term_is_integer(t)) {
        avm_int64_t tmp = term_maybe_unbox_int64(t);
        if (tmp < 0 || tmp > 255) {
            return InteropBadArg;
        }
        uint8_t val = (uint8_t) tmp;
        md5Update(ctx, &val, 1);
    } else /* term_is_binary(t) */ {
        md5Update(ctx, (uint8_t *) term_binary_data(t), term_binary_size(t));
    }
    return InteropOk;
}

static bool do_md5_hash_vendored(term data, unsigned char *dst)
{
    MD5Context ctx;
    md5Init(&ctx);

    InteropFunctionResult result = interop_chardata_fold(data, md5_hash_vendored_fold_fun, NULL, (void *) &ctx);
    if (UNLIKELY(result != InteropOk)) {
        return false;
    }

    md5Finalize(&ctx);
    memcpy(dst, ctx.digest, 16);

    return true;
}

#define MAX_MD_SIZE 64
static term nif_erlang_md5(Context *ctx, int argc, term argv[])
{
    UNUSED(argc);
    term data = argv[0];

    if (!(term_is_binary(data) || term_is_list(data))) {
        RAISE_ERROR(BADARG_ATOM)
    }

    unsigned char digest[MAX_MD_SIZE];
    size_t digest_len = 16;

    if (UNLIKELY(!do_md5_hash_vendored(data, digest))) {
        RAISE_ERROR(BADARG_ATOM)
    }

    if (UNLIKELY(memory_ensure_free(ctx, term_binary_heap_size(digest_len)) != MEMORY_GC_OK)) {
        RAISE_ERROR(OUT_OF_MEMORY_ATOM);
    }
    return term_from_literal_binary(digest, digest_len, &ctx->heap, ctx->global);
}

static const struct Nif md5_nif = {
    .base.type = NIFFunctionType,
    .nif_ptr = nif_erlang_md5
};

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

static term nif_rand_splitmix64_next(Context *ctx, int argc, term argv[])
{
    UNUSED(argc);
    VALIDATE_VALUE(argv[0], term_is_any_integer);
    uint64_t x = term_maybe_unbox_int64(argv[0]);
    // implementation based on https://github.com/erlang/otp/blob/d051172925a5c84b2f21850a188a533f885f201c/lib/stdlib/src/rand.erl#L1629
    uint64_t z = (x += 0x9e3779b97f4a7c15);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
    z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
    z = z ^ (z >> 31);
    // assume pessimisticly both ints will be boxed
    if (UNLIKELY(memory_ensure_free_opt(ctx, TUPLE_SIZE(2) + 2 * BOXED_INT64_SIZE, MEMORY_CAN_SHRINK) != MEMORY_GC_OK)) {
        RAISE_ERROR(OUT_OF_MEMORY_ATOM);
    }
    term result = term_alloc_tuple(2, &ctx->heap);
    term_put_tuple_element(result, 0, term_make_maybe_boxed_int64(z, &ctx->heap));
    term_put_tuple_element(result, 1, term_make_maybe_boxed_int64(x, &ctx->heap));
    return result;
}

static const struct Nif rand_splitmix64_next_nif = {
    .base.type = NIFFunctionType,
    .nif_ptr = nif_rand_splitmix64_next
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

static term nif_ets_new(Context *ctx, int argc, term argv[])
{
    assert(argc == 2);

    term name = argv[0];
    term options = argv[1];

    VALIDATE_VALUE(name, term_is_atom);
    VALIDATE_VALUE(options, term_is_list);

    term is_named = interop_kv_get_value_default(options, ATOM_STR("\xB", "named_table"), FALSE_ATOM, ctx->global);
    term keypos = interop_kv_get_value_default(options, ATOM_STR("\x6", "keypos"), term_from_int(1), ctx->global);
    avm_int_t key_index = term_to_int(keypos) - 1;

    if (UNLIKELY(key_index < 0)) {
        RAISE_ERROR(BADARG_ATOM);
    }

    term private = interop_kv_get_value(options, ATOM_STR("\x7", "private"), ctx->global);
    term public = interop_kv_get_value(options, ATOM_STR("\x6", "public"), ctx->global);

    // NOTE: If multiple accesses are specified, the precedence is: public > private > protected
    Popcorn2EtsTableAccess access = Popcorn2EtsTableAccessProtected;
    if (!term_is_invalid_term(private)) {
        access = Popcorn2EtsTableAccessPrivate;
    }
    if (!term_is_invalid_term(public)) {
        access = Popcorn2EtsTableAccessPublic;
    }

    term bag = interop_kv_get_value(options, ATOM_STR("\x3", "bag"), ctx->global);
    term duplicate_bag = interop_kv_get_value(options, ATOM_STR("\xd", "duplicate_bag"), ctx->global);

    // NOTE: If multiple table types are specified, the precedence is: duplicate_bag > bag > set
    Popcorn2EtsTableType type = Popcorn2EtsTableSet;
    if (!term_is_invalid_term(bag)) {
        type = Popcorn2EtsTableBag;
    }
    if (!term_is_invalid_term(duplicate_bag)) {
        type = Popcorn2EtsTableDuplicateBag;
    }

    term table = term_invalid_term();

    Popcorn2EtsStatus result = popcorn2_ets_create_table(
        name,
        is_named == TRUE_ATOM,
        type,
        access,
        (size_t) key_index,
        &table,
        ctx);

    switch (result) {
        case Popcorn2EtsOk:
            return table;
        case Popcorn2EtsTableNameExists:
            RAISE_ERROR(BADARG_ATOM);
        case Popcorn2EtsAllocationError:
            RAISE_ERROR(MEMORY_ATOM);
        default:
            // unreachable
            AVM_ABORT();
    }
}

static inline bool is_ets_table_id(term t)
{
    return term_is_reference(t) || term_is_atom(t);
}

static term nif_ets_insert(Context *ctx, int argc, term argv[])
{
    assert(argc == 2);

    term name_or_ref = argv[0];
    term entry = argv[1];

    VALIDATE_VALUE(name_or_ref, is_ets_table_id);

    Popcorn2EtsStatus result = popcorn2_ets_insert(name_or_ref, entry, false, ctx);

    switch (result) {
        case Popcorn2EtsOk:
            return TRUE_ATOM;
        case Popcorn2EtsBadAccess:
        case Popcorn2EtsBadEntry:
            RAISE_ERROR(BADARG_ATOM);
        case Popcorn2EtsAllocationError:
            RAISE_ERROR(OUT_OF_MEMORY_ATOM);
        default:
            // unreachable
            AVM_ABORT();
    }
}

static term nif_ets_insert_new(Context *ctx, int argc, term argv[])
{
    assert(argc == 2);

    term name_or_ref = argv[0];
    term entry = argv[1];

    VALIDATE_VALUE(name_or_ref, is_ets_table_id);

    Popcorn2EtsStatus result = popcorn2_ets_insert(name_or_ref, entry, true, ctx);

    switch (result) {
        case Popcorn2EtsOk:
            return TRUE_ATOM;
        case Popcorn2EtsKeyExists:
            return FALSE_ATOM;
        case Popcorn2EtsBadAccess:
        case Popcorn2EtsBadEntry:
            RAISE_ERROR(BADARG_ATOM);
        case Popcorn2EtsAllocationError:
            RAISE_ERROR(OUT_OF_MEMORY_ATOM);
        default:
            // unreachable
            AVM_ABORT();
    }
}

static term nif_ets_lookup(Context *ctx, int argc, term argv[])
{
    assert(argc == 2);

    term name_or_ref = argv[0];
    term key = argv[1];

    VALIDATE_VALUE(name_or_ref, is_ets_table_id);

    term ret = term_invalid_term();

    Popcorn2EtsStatus result = popcorn2_ets_lookup(name_or_ref, key, &ret, ctx);

    switch (result) {
        case Popcorn2EtsOk:
            return ret;
        case Popcorn2EtsBadAccess:
            RAISE_ERROR(BADARG_ATOM);
        case Popcorn2EtsAllocationError:
            RAISE_ERROR(OUT_OF_MEMORY_ATOM);
        default:
            // unreachable
            AVM_ABORT();
    }
}

static term nif_ets_member(Context *ctx, int argc, term argv[])
{
    UNUSED(argc);

    term ref = argv[0];
    VALIDATE_VALUE(ref, is_ets_table_id);

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
    VALIDATE_VALUE(ref, is_ets_table_id);

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
    VALIDATE_VALUE(ref, is_ets_table_id);

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
    VALIDATE_VALUE(ref, is_ets_table_id);

    term key = argv[1];
    term element_spec = argv[2];
    term ret = term_invalid_term();
    PopcornEtsErrorCode result = popcorn_ets_update_element(ref, key, element_spec, &ret, ctx);
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
    assert(argc == 3 || argc == 4);

    term name_or_ref = argv[0];
    term key = argv[1];
    term pos = argv[2];

    VALIDATE_VALUE(name_or_ref, is_ets_table_id);
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

    Popcorn2EtsStatus result = popcorn2_ets_lookup_element(name_or_ref, key, (size_t) index, &ret, ctx);

    switch (result) {
        case Popcorn2EtsOk:
            if (!term_is_nil(ret)) {
                return ret;
            }

            if (!term_is_invalid_term(default_value)) {
                return default_value;
            }

            RAISE_ERROR(BADARG_ATOM);
        case Popcorn2EtsBadAccess:
        case Popcorn2EtsBadIndex:
            RAISE_ERROR(BADARG_ATOM);
        case Popcorn2EtsAllocationError:
            RAISE_ERROR(MEMORY_ATOM);
        default:
            AVM_ABORT();
    }
}

static term nif_ets_delete(Context *ctx, int argc, term argv[])
{
    assert(argc == 1 || argc == 2);

    term name_or_ref = argv[0];

    VALIDATE_VALUE(name_or_ref, is_ets_table_id);

    Popcorn2EtsStatus result;

    if (argc == 1) {
        result = popcorn2_ets_delete_table(name_or_ref, ctx);
    } else {
        result = popcorn2_ets_delete(name_or_ref, argv[1], ctx);
    }

    switch (result) {
        case Popcorn2EtsOk:
            return TRUE_ATOM;
        case Popcorn2EtsBadAccess:
            RAISE_ERROR(BADARG_ATOM);
        default:
            // unreachable
            AVM_ABORT();
    }
}

static term nif_ets_delete_object(Context *ctx, int argc, term argv[])
{
    assert(argc == 2);

    term name_or_ref = argv[0];
    term tuple = argv[1];

    VALIDATE_VALUE(name_or_ref, is_ets_table_id);
    VALIDATE_VALUE(tuple, term_is_tuple);

    Popcorn2EtsStatus result = popcorn2_ets_delete_object(name_or_ref, tuple, ctx);

    switch (result) {
        case Popcorn2EtsOk:
            return TRUE_ATOM;
        case Popcorn2EtsBadAccess:
        case Popcorn2EtsBadEntry:
            RAISE_ERROR(BADARG_ATOM);
        case Popcorn2EtsAllocationError:
            RAISE_ERROR(MEMORY_ATOM);
        default:
            // unreachable
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

/*
 * This file is part of AtomVM.
 *
 * Copyright 2022 Fred Dushin <fred@dushin.net>
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

#include "stacktrace.h"
#include "defaultatoms.h"
#include "globalcontext.h"
#include "interop.h"
#include "memory.h"

#ifndef AVM_CREATE_STACKTRACES

term stacktrace_create_raw(Context *ctx, Module *mod, int current_offset, term exception_class)
{
    UNUSED(ctx);
    UNUSED(mod);
    UNUSED(current_offset);
    return exception_class;
}

term stacktrace_build(Context *ctx, term *stack_info, uint32_t live)
{
    UNUSED(ctx);
    UNUSED(stack_info);
    UNUSED(live);
    return UNDEFINED_ATOM;
}

term stacktrace_exception_class(term stack_info)
{
    return stack_info;
}

void stacktrace_print(FILE *fd, term stacktrace, const Context *ctx)
{
    UNUSED(stacktrace);
    UNUSED(ctx);
    fprintf(fd, "No stacktrace created, enable with AVM_CREATE_STACKTRACES\n");
}

#else

static void cp_to_mod_lbl_off(term cp, Context *ctx, Module **cp_mod, int *label, int *l_off, long *mod_offset)
{
    int module_index = cp >> 24;
    Module *mod = globalcontext_get_module_by_index(ctx->global, module_index);
    *mod_offset = (cp & 0xFFFFFF) >> 2;

    *cp_mod = mod;

    uint8_t *code = &mod->code->code[0];
    int labels_count = ENDIAN_SWAP_32(mod->code->labels);

    int i = 1;
    const uint8_t *l = mod->labels[1];
    while (*mod_offset > l - code) {
        ++i;
        if (i >= labels_count) {
            // last label + 1 is reserved for end of module.
            *label = i;
            *l_off = 0;
            return;
        }
        l = mod->labels[i];
    }

    *label = i - 1;
    *l_off = *mod_offset - (mod->labels[*label] - code);
}

struct ModuleOffsetPair
{
    Module *module;
    int offset;
};

term stacktrace_create_raw(Context *ctx, Module *mod, int current_offset, term exception_class)
{
    term result;
    term *stack_base = context_stack_base(ctx);
    unsigned long stack_size = context_stack_size(ctx) + 1;
    struct ModuleOffsetPair *frames_modules = malloc(stack_size * sizeof(struct ModuleOffsetPair));
    if (IS_NULL_PTR(frames_modules)) {
        fprintf(stderr, "Unable to allocate space for modules list.  No stacktrace will be created\n");
        result = UNDEFINED_ATOM;
        goto stacktrace_create_raw_cleanup;
    }

    frames_modules[0] = (struct ModuleOffsetPair){ .module = mod, .offset = current_offset };

    unsigned int num_frames = 1;
    Module *prev_mod = NULL;
    long prev_mod_offset = -1;
    for (term *ct = ctx->e; ct < stack_base; ++ct) {
        Module *mod = NULL;
        long mod_offset = -1;
        if (term_is_cp(*ct)) {
            Module *cp_mod;
            int label;
            int offset;

            cp_to_mod_lbl_off(*ct, ctx, &cp_mod, &label, &offset, &mod_offset);
            if (mod_offset != cp_mod->end_instruction_ii && !(prev_mod == cp_mod && mod_offset == prev_mod_offset)) {
                mod = cp_mod;
            }
        } else if (term_is_catch_label(*ct)) {
            int module_index;
            int label = term_to_catch_label_and_module(*ct, &module_index);

            Module *cl_mod = globalcontext_get_module_by_index(ctx->global, module_index);
            uint8_t *code = &cl_mod->code->code[0];
            mod_offset = cl_mod->labels[label] - code;

            if (!(prev_mod == cl_mod && mod_offset == prev_mod_offset)) {
                mod = cl_mod;
            }
        }

        if (mod) {
            frames_modules[num_frames] = (struct ModuleOffsetPair){ .module = mod, .offset = mod_offset };
            prev_mod = mod;
            prev_mod_offset = mod_offset;
            ++num_frames;
        }
    }

    // {num_frames, num_aux_terms, filename_lens, num_mods, [{module, offset}, ...]}
    size_t requested_size = TUPLE_SIZE(6) + num_frames * (2 + TUPLE_SIZE(2));
    // We need to preserve x0 and x1 that contain information on the current exception
    if (UNLIKELY(memory_ensure_free_with_roots(ctx, requested_size, 2, ctx->x, MEMORY_CAN_SHRINK) != MEMORY_GC_OK)) {
        fprintf(stderr, "WARNING: Unable to allocate heap space for raw stacktrace\n");
        result = OUT_OF_MEMORY_ATOM;
        goto stacktrace_create_raw_cleanup;
    }

    term raw_stacktrace = term_nil();

    for (size_t i = 0; i < num_frames; ++i) {
        term frame_info = term_alloc_tuple(2, &ctx->heap);
        term_put_tuple_element(frame_info, 0, term_from_int(frames_modules[i].module->module_index));
        term_put_tuple_element(frame_info, 1, term_from_int(frames_modules[i].offset));
        raw_stacktrace = term_list_prepend(frame_info, raw_stacktrace, &ctx->heap);
    }

    // Needed for heap memory allocation
    unsigned int num_aux_terms = 0;
    unsigned int filename_lens = 0;
    unsigned int num_mods = 0;
    for (size_t i = 0; i < num_frames; ++i) {
        Module *mod = frames_modules[i].module;
        if (module_has_line_chunk(mod)) {
            // If module occurs more than once in the stacktrace
            // the path term is reused
            bool module_reused = false;
            for (size_t j = 0; j < i; ++j) {
                if (frames_modules[j].module == mod) {
                    module_reused = true;
                    break;
                }
            }
            if (!module_reused) {
                for (size_t j = 0; j < mod->num_filenames; ++j) {
                    filename_lens += mod->filenames[j].len;
                }
                num_mods += 1;
            }
            num_aux_terms += 1;
        }
    }

    term stack_info = term_alloc_tuple(6, &ctx->heap);
    term_put_tuple_element(stack_info, 0, term_from_int(num_frames));
    term_put_tuple_element(stack_info, 1, term_from_int(num_aux_terms));
    term_put_tuple_element(stack_info, 2, term_from_int(filename_lens));
    term_put_tuple_element(stack_info, 3, term_from_int(num_mods));
    term_put_tuple_element(stack_info, 4, raw_stacktrace);
    term_put_tuple_element(stack_info, 5, exception_class);

    result = stack_info;

stacktrace_create_raw_cleanup:
    free(frames_modules);
    return result;
}

term stacktrace_exception_class(term stack_info)
{
    return term_get_tuple_element(stack_info, 5);
}

struct ModulePathPair
{
    struct ModuleFilename *module_filename;
    term path;
};

term stacktrace_build(Context *ctx, term *stack_info, uint32_t live)
{
    term result;
    GlobalContext *glb = ctx->global;
    struct ModulePathPair *modules_paths = NULL;

    if (*stack_info == OUT_OF_MEMORY_ATOM) {
        result = *stack_info;
        goto stacktrace_build_cleanup;
    }
    if (!term_is_tuple(*stack_info)) {
        result = UNDEFINED_ATOM;
        goto stacktrace_build_cleanup;
    }

    int num_frames = term_to_int(term_get_tuple_element(*stack_info, 0));
    int num_aux_terms = term_to_int(term_get_tuple_element(*stack_info, 1));
    int filename_lens = term_to_int(term_get_tuple_element(*stack_info, 2));
    int num_mods = term_to_int(term_get_tuple_element(*stack_info, 3));

    modules_paths = malloc(num_mods * sizeof(struct ModulePathPair));
    if (IS_NULL_PTR(modules_paths)) {
        fprintf(stderr, "Unable to allocate space for module paths.  Returning raw stacktrace.\n");
        result = *stack_info;
        goto stacktrace_build_cleanup;
    }

    //
    // [{module, function, arity, [{file, string()}, {line, int}]}, ...]
    //
    size_t requested_size = (TUPLE_SIZE(4) + 2) * num_frames + num_aux_terms * (4 + 2 * TUPLE_SIZE(2)) + 2 * filename_lens;
    if (UNLIKELY(memory_ensure_free_with_roots(ctx, requested_size, live, ctx->x, MEMORY_CAN_SHRINK) != MEMORY_GC_OK)) {
        result = OUT_OF_MEMORY_ATOM;
        goto stacktrace_build_cleanup;
    }

    term raw_stacktrace = term_get_tuple_element(*stack_info, 4);

    term stacktrace = term_nil();
    term el = raw_stacktrace;
    int module_path_idx = 0;
    while (!term_is_nil(el)) {
        term mod_index_tuple = term_get_list_head(el);
        term cp = module_address(
            term_to_int(term_get_tuple_element(mod_index_tuple, 0)),
            term_to_int(term_get_tuple_element(mod_index_tuple, 1)));

        Module *cp_mod;
        int label;
        int offset;
        long mod_offset;
        cp_to_mod_lbl_off(cp, ctx, &cp_mod, &label, &offset, &mod_offset);

        term module_name = module_get_name(cp_mod);

        term frame_i = term_alloc_tuple(4, &ctx->heap);
        term_put_tuple_element(frame_i, 0, module_name);

        term aux_data = term_nil();
        if (module_has_line_chunk(cp_mod)) {
            term line_tuple = term_alloc_tuple(2, &ctx->heap);
            term_put_tuple_element(line_tuple, 0, globalcontext_make_atom(glb, ATOM_STR("\x4", "line")));
            struct LineRef line_ref = module_find_line(cp_mod, (unsigned int) mod_offset);
            term_put_tuple_element(line_tuple, 1, line_ref.line_idx == -1 ? UNDEFINED_ATOM : term_from_int(line_ref.line_idx));
            aux_data = term_list_prepend(line_tuple, aux_data, &ctx->heap);

            term file_tuple = term_alloc_tuple(2, &ctx->heap);
            term_put_tuple_element(file_tuple, 0, globalcontext_make_atom(glb, ATOM_STR("\x4", "file")));

            struct ModuleFilename *filename = &cp_mod->filenames[line_ref.filename_idx];
            // Reuse path term if already created
            term path = term_invalid_term();
            for (int i = 0; i < module_path_idx; ++i) {
                if (modules_paths[i].module_filename == filename) {
                    path = modules_paths[i].path;
                    break;
                }
            }
            if (term_is_invalid_term(path)) {
                path = term_from_string((const uint8_t *) filename->data, filename->len, &ctx->heap);
                modules_paths[module_path_idx].module_filename = filename;
                modules_paths[module_path_idx].path = path;
                ++module_path_idx;
            }
            term_put_tuple_element(file_tuple, 1, path);
            aux_data = term_list_prepend(file_tuple, aux_data, &ctx->heap);
        }
        term_put_tuple_element(frame_i, 3, aux_data);

        AtomString function_name = NULL;
        int arity = 0;
        bool result = module_get_function_from_label(cp_mod, label, &function_name, &arity, glb);

        if (LIKELY(result)) {
            term_put_tuple_element(frame_i, 1, globalcontext_make_atom(glb, function_name));
            term_put_tuple_element(frame_i, 2, term_from_int(arity));
        } else {
            term_put_tuple_element(frame_i, 1, UNDEFINED_ATOM);
            term_put_tuple_element(frame_i, 2, term_from_int(0));
        }
        stacktrace = term_list_prepend(frame_i, stacktrace, &ctx->heap);

        el = term_get_list_tail(el);
    }

    result = stacktrace;

stacktrace_build_cleanup:
    free(modules_paths);
    return result;
}

void stacktrace_print(FILE *fd, term stacktrace, const Context *ctx)
{
    fprintf(stderr, "\nStacktrace:\n");
    while (!term_is_nil(stacktrace)) {
        term frame = term_get_list_head(stacktrace);
        term location = term_get_tuple_element(frame, 3);
        // The following code assumes the shape of the 'location' list
        term file_tuple = term_get_list_head(location);
        int ok = 0;
        char *file = interop_term_to_string(term_get_tuple_element(file_tuple, 1), &ok);
        assert(ok == 1);
        term line_tuple = term_get_list_head(term_get_list_tail(location));
        term line = term_get_tuple_element(line_tuple, 1);
        term module = term_get_tuple_element(frame, 0);
        term fun = term_get_tuple_element(frame, 1);
        term arity = term_get_tuple_element(frame, 2);
        fprintf(fd, "\t%s:", file);
        term_display(fd, line, ctx);
        fprintf(fd, ": ");
        term_display(fd, module, ctx);
        fprintf(fd, ":");
        term_display(fd, fun, ctx);
        fprintf(fd, "/");
        term_display(fd, arity, ctx);
        fprintf(fd, "\n");
        free(file);
        stacktrace = term_get_list_tail(stacktrace);
    }
    fprintf(fd, "\n");
}

#endif

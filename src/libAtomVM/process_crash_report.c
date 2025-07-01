#include "process_crash_report.h"
#include "module.h"
#include "stacktrace.h"

#ifndef AVM_PROCESS_CRASH_DUMPS

COLD_FUNC void process_crash_report_print(Context *ctx)
{
    (void) ctx;
}

#else

COLD_FUNC static void cp_to_mod_lbl_off(term cp, Context *ctx, Module **cp_mod, int *label, int *l_off)
{
    Module *mod = globalcontext_get_module_by_index(ctx->global, MODULE_INDEX_FROM_CP(cp));
    long mod_offset = MODULE_OFFSET_FROM_CP(cp);

    *cp_mod = mod;

    uint8_t *code = &mod->code->code[0];
    int labels_count = ENDIAN_SWAP_32(mod->code->labels);

    int i = 1;
    const uint8_t *l = mod->labels[1];
    while (mod_offset > l - code) {
        i++;
        if (i >= labels_count) {
            // last label + 1 is reserved for end of module.
            *label = i;
            *l_off = 0;
            return;
        }
        l = mod->labels[i];
    }

    *label = i - 1;
    *l_off = mod_offset - (mod->labels[*label] - code);
}

COLD_FUNC void process_crash_report_print(Context *ctx)
{
    fprintf(stderr, "CRASH \n======\n");

    fprintf(stderr, "pid: ");
    term_display(stderr, term_from_local_process_id(ctx->process_id), ctx);
    fprintf(stderr, "\n");

    term stacktrace = stacktrace_ensure_built(ctx, &ctx->x[2], 3);
    stacktrace_print(stderr, stacktrace, ctx);

    {
        Module *cp_mod;
        int label;
        int offset;
        cp_to_mod_lbl_off(ctx->cp, ctx, &cp_mod, &label, &offset);
        fprintf(stderr, "cp: #CP<module: %i, label: %i, offset: %i>\n\n",
            cp_mod->module_index, label, offset);
    }

    fprintf(stderr, "x[0]: ");
    term_display(stderr, ctx->x[0], ctx);
    fprintf(stderr, "\nx[1]: ");
    term_display(stderr, ctx->x[1], ctx);
    fprintf(stderr, "\nx[2]: ");
    term_display(stderr, ctx->x[2], ctx);
    fprintf(stderr, "\n\nStack \n------\n\n");

    term *ct = ctx->e;

    while (ct != ctx->heap.heap_end) {
        if (term_is_catch_label(*ct)) {
            int target_module;
            int target_label = term_to_catch_label_and_module(*ct, &target_module);
            fprintf(stderr, "catch: %i:%i\n", target_label, target_module);

        } else if (term_is_cp(*ct)) {
            Module *cp_mod;
            int label;
            int offset;
            cp_to_mod_lbl_off(*ct, ctx, &cp_mod, &label, &offset);
            fprintf(stderr, "#CP<module: %i, label: %i, offset: %i>\n", cp_mod->module_index, label, offset);

        } else {
            term_display(stderr, *ct, ctx);
            fprintf(stderr, "\n");
        }

        ct++;
    }

    fprintf(stderr, "\n\nMailbox\n--------\n");
    mailbox_crashdump(ctx);

    fprintf(stderr, "\n\nMonitors\n--------\n");
    // Lock processes table to make sure any dying process will not modify monitors
    struct ListHead *processes_table = synclist_rdlock(&ctx->global->processes_table);
    UNUSED(processes_table);
    struct ListHead *item;
    LIST_FOR_EACH (item, &ctx->monitors_head) {
        struct Monitor *monitor = GET_LIST_ENTRY(item, struct Monitor, monitor_list_head);
        if (term_is_pid(monitor->monitor_obj)) {
            term_display(stderr, monitor->monitor_obj, ctx);
        } else {
            fprintf(stderr, "<resource %p>", (void *) term_to_const_term_ptr(monitor->monitor_obj));
        }
        fprintf(stderr, " ");
        if (monitor->ref_ticks == 0) {
            fprintf(stderr, "<");
        }
        fprintf(stderr, "---> ");
        term_display(stderr, term_from_local_process_id(ctx->process_id), ctx);
        fprintf(stderr, "\n");
    }
    synclist_unlock(&ctx->global->processes_table);
    fprintf(stderr, "\n\n**End Of Crash Report**\n");
}
#endif

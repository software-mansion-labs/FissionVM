// See popcorn_nifs.gperf for status of each NIF

#include "popcorn_nifs.h"

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
#include "external_term.h"
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

static term nif_mock_popcorn_nif(Context *ctx, int argc, term argv[])
{
    UNUSED(ctx);
    UNUSED(argc);
    UNUSED(argv);
    return ERROR_ATOM;
}

static const struct Nif mock_popcorn_nif = {
    .base.type = NIFFunctionType,
    .nif_ptr = nif_mock_popcorn_nif
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

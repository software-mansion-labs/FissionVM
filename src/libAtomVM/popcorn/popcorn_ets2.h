/*
 * This file is part of AtomVM.
 *
 * Copyright 2024 Fred Dushin <fred@dushin.net>
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

#ifndef _POPCORN2_ETS_H_
#define _POPCORN2_ETS_H_

#include <stdbool.h>

struct GlobalContext;
struct Context;

#include "../synclist.h"
#include "../term.h"

#ifdef __cplusplus
extern "C" {
#endif

// NOTE: Ordered set is not currently supported
typedef enum Popcorn2EtsTableType
{
    Popcorn2EtsTableSet,
    Popcorn2EtsTableOrderedSet,
    Popcorn2EtsTableBag,
    Popcorn2EtsTableDuplicateBag
} Popcorn2EtsTableType;

typedef enum Popcorn2EtsTableAccess
{
    Popcorn2EtsTableAccessPrivate,
    Popcorn2EtsTableAccessProtected,
    Popcorn2EtsTableAccessPublic
} Popcorn2EtsTableAccess;

typedef enum Popcorn2EtsStatus
{
    Popcorn2EtsOk,
    Popcorn2EtsKeyExists,
    Popcorn2EtsTableNameExists,
    Popcorn2EtsTupleNotExists,
    Popcorn2EtsBadEntry,
    Popcorn2EtsBadAccess,
    Popcorn2EtsBadIndex,
    Popcorn2EtsAllocationError
} Popcorn2EtsStatus;

typedef struct Popcorn2Ets
{
    struct SyncList ets_tables;
} Popcorn2Ets;

void popcorn2_ets_init(Popcorn2Ets *ets);
void popcorn2_ets_destroy(Popcorn2Ets *ets, GlobalContext *global);

Popcorn2EtsStatus popcorn2_ets_create_table(
    term name,
    bool named,
    Popcorn2EtsTableType type,
    Popcorn2EtsTableAccess access,
    size_t index,
    term *ret,
    Context *ctx);
void popcorn2_ets_delete_owned_tables(Popcorn2Ets *ets, int32_t process_id, GlobalContext *global);

Popcorn2EtsStatus popcorn2_ets_lookup(term name_or_ref, term key, term *ret, Context *ctx);
Popcorn2EtsStatus popcorn2_ets_lookup_element(term name_or_ref, term key, size_t index, term *ret, Context *ctx);
Popcorn2EtsStatus popcorn2_ets_insert(term name_or_ref, term entry, bool new, Context *ctx);
Popcorn2EtsStatus popcorn2_ets_update_element(term name_or_ref, term key, term element_spec, term default_tuple, Context *ctx);
Popcorn2EtsStatus popcorn2_ets_delete(term name_or_ref, term key, Context *ctx);
Popcorn2EtsStatus popcorn2_ets_delete_table(term name_or_ref, Context *ctx);
Popcorn2EtsStatus popcorn2_ets_delete_object(term name_or_ref, term tuple, Context *ctx);
Popcorn2EtsStatus popcorn2_ets_take(term name_or_ref, term key, term *ret, Context *ctx);

#ifdef __cplusplus
}
#endif

#endif // _POPCORN2_ETS_H_

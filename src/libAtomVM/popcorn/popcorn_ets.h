/*
 * This file is part of AtomVM.
 *
 * Copyright 2024 Fred Dushin <fred@dushin.net>
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

#ifndef _POPCORN_ETS_H_
#define _POPCORN_ETS_H_

struct Context;
struct GlobalContext;

#include "list.h"
#include "synclist.h"
#include "term.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// N.B. Only PopcornEtsTableSet currently supported
typedef enum PopcornEtsTableType
{
    PopcornEtsTableSet,
    PopcornEtsTableOrderedSet,
    PopcornEtsTableBag,
    PopcornEtsTableDuplicateBag
} PopcornEtsTableType;

typedef enum PopcornEtsAccessType
{
    PopcornEtsAccessPrivate,
    PopcornEtsAccessProtected,
    PopcornEtsAccessPublic
} PopcornEtsAccessType;

typedef enum PopcornEtsErrorCode
{
    PopcornEtsOk,
    PopcornEtsBadAccess,
    PopcornEtsTableNameInUse,
    PopcornEtsBadEntry,
    PopcornEtsAllocationFailure,
    PopcornEtsEntryNotFound,
    PopcornEtsBadPosition
} PopcornEtsErrorCode;
struct PopcornEts
{
    // TODO Using a list imposes O(len(popcorn_ets_tables)) cost
    // on lookup, so in the future we may want to consider
    // a table or map instead of a list.
    struct SyncList popcorn_ets_tables;
};

void popcorn_ets_init(struct PopcornEts *popcorn_ets);
void popcorn_ets_destroy(struct PopcornEts *popcorn_ets, GlobalContext *global);

PopcornEtsErrorCode popcorn_ets_create_table(term name, bool is_named, PopcornEtsTableType table_type, PopcornEtsAccessType access_type, size_t keypos, term *ret, Context *ctx);
void popcorn_ets_delete_owned_tables(struct PopcornEts *popcorn_ets, int32_t process_id, GlobalContext *global);

PopcornEtsErrorCode popcorn_ets_insert(term ref, term entry, bool *entry_inserted, Context *ctx);
PopcornEtsErrorCode popcorn_ets_lookup(term ref, term key, term *ret, Context *ctx);
PopcornEtsErrorCode popcorn_ets_lookup_element(term ref, term key, size_t pos, term *ret, Context *ctx);
PopcornEtsErrorCode popcorn_ets_delete(term ref, term key, term *ret, Context *ctx);
PopcornEtsErrorCode popcorn_ets_drop_table(term ref, term *ret, Context *ctx);
PopcornEtsErrorCode popcorn_ets_update_counter(term ref, term key, term operation, term default_value, term *ret, Context *ctx);
PopcornEtsErrorCode popcorn_ets_delete_object(term ref, term tuple, term *ret, Context *ctx);
PopcornEtsErrorCode popcorn_ets_update_element(term ref, term key, term value, term pos, term *ret, Context *ctx);
PopcornEtsErrorCode popcorn_ets_take(term ref, term key, term *ret, Context *ctx);

#ifdef __cplusplus
}
#endif

#endif

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

#ifndef _POPCORN_ETS_HASHTABLE_H_
#define _POPCORN_ETS_HASHTABLE_H_

#include "globalcontext.h"
#include "term.h"
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NUM_BUCKETS 16

struct PopcornEtsHashTableEntry
{
    term key;
    term entry;
    Heap *heap;
};

struct PopcornEtsHashTable
{
    size_t capacity;
    struct HNode *buckets[NUM_BUCKETS];
};

typedef enum PopcornEtsHashtableOptions
{
    PopcornEtsHashtableAllowOverwrite = (1 << 0),
} PopcornEtsHashtableOptions;

typedef enum PopcornEtsHashtableErrorCode
{
    PopcornEtsHashtableOk = 0,
    PopcornEtsHashtableKeyAlreadyExists,
    PopcornEtsHashtableError
} PopcornEtsHashtableErrorCode;

struct PopcornEtsHashTable *popcorn_ets_hashtable_new();
void popcorn_ets_hashtable_destroy(struct PopcornEtsHashTable *hash_table, GlobalContext *global);

PopcornEtsHashtableErrorCode popcorn_ets_hashtable_insert(struct PopcornEtsHashTable *hash_table, term key, term entry, PopcornEtsHashtableOptions opts, Heap *entry_heap, GlobalContext *global);
term popcorn_ets_hashtable_lookup(struct PopcornEtsHashTable *hash_table, term key, GlobalContext *global);
bool popcorn_ets_hashtable_remove(struct PopcornEtsHashTable *hash_table, term key, struct PopcornEtsHashTableEntry *removed, GlobalContext *global);

#ifdef __cplusplus
}
#endif

#endif

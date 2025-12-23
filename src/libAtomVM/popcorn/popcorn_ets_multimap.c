/*
 * This file is part of AtomVM.
 *
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

#include "popcorn_ets_multimap.h"
#include "popcorn_ets_multimap_hash.h"

EtsMultimap *ets_multimap_new(EtsMultimapType type, size_t index)
{
    // TODO
    return NULL;
}

void ets_multimap_delete(EtsMultimap *multimap, GlobalContext *global)
{
    // TODO
}

EtsMultimapStatus ets_multimap_insert(
    EtsMultimap *multimap,
    term *tuples,
    size_t count,
    GlobalContext *global)
{
    // TODO
    return EtsMultimapOk;    
}

EtsMultimapStatus ets_multimap_lookup(
    EtsMultimap *multimap,
    term key,
    term **tuples,
    size_t *count,
    GlobalContext *global)
{
    // TODO
    return EtsMultimapOk;
}

EtsMultimapStatus ets_multimap_remove(
    EtsMultimap *multimap,
    term key,
    GlobalContext *global)
{
    // TODO
    return EtsMultimapOk;
}

/*
 * This file is part of AtomVM.
 *
 * Copyright 2023 Paul Guyot <pguyot@kallisys.net>
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
Module["cast"] = function (name, message) {
  ccall("cast", "void", ["string", "string"], [name, message]);
};
Module["call"] = async function (name, message) {
  const promiseId = ccall(
    "call",
    "integer",
    ["string", "string"],
    [name, message],
  );
  return promiseMap.get(promiseId).promise;
};
Module["nextRemoteObjectKey"] = function () {
  return ccall("next_remote_object_key", "integer", [], []);
};
Module["nextTrackedObjectKey"] = function () {
  return ccall("next_tracked_object_key", "integer", [], []);
};
Module["remoteObjectsMap"] = new Map();
Module["onRemoteObjectDelete"] = (_key) => {};
Module["onRunTrackedJs"] = (scriptString, isDebug) => {
  const indirectEval = eval;
  const result = indirectEval(scriptString);
  if (isDebug) {
    if (!Array.isArray(result) && result !== undefined) {
      throw new Error(
        "Script passed to onRunTrackedJs() returned invalid value, accepted values are arrays and undefined",
      );
    }
  }

  return (
    result?.map((value) => {
      const key = Module["nextRemoteObjectKey"]();
      Module["remoteObjectsMap"].set(key, value);
      return key;
    }) ?? []
  );
};

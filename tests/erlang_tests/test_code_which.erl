%
% This file is part of AtomVM.
%
% Copyright 2025 Franciszek Kubis <franciszek.kubis@swmansion.com>
%
% Licensed under the Apache License, Version 2.0 (the "License");
% you may not use this file except in compliance with the License.
% You may obtain a copy of the License at
%
%    http://www.apache.org/licenses/LICENSE-2.0
%
% Unless required by applicable law or agreed to in writing, software
% distributed under the License is distributed on an "AS IS" BASIS,
% WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
% See the License for the specific language governing permissions and
% limitations under the License.
%
% SPDX-License-Identifier: Apache-2.0 OR LGPL-2.1-or-later
%

-module(test_code_which).

-export([start/0]).

-include("code_load/export_test_module_data.hrl").

start() ->
    ok = code_which_new_loaded_module(),
    ok = code_which_loaded_module(),
    ok = code_which_non_existing_module(),
    ok = code_which_wrong_argument("a string"),
    ok = code_which_wrong_argument(123),
    ok = code_which_wrong_argument({1, "a"}),
    ok = code_which_wrong_argument([1, b, 3]),
    0.

code_which_new_loaded_module() ->
    Bin = ?EXPORT_TEST_MODULE_DATA,
    non_existing = code:which(export_test_module),
    {module, export_test_module} = code:load_binary(
        export_test_module, "export_test_module", Bin
    ),
    "export_test_module" = code:which(export_test_module),
    ok.

code_which_loaded_module() ->
    "test_code_which" = code:which(?MODULE),
    ok.

code_which_non_existing_module() ->
    non_existing = code:which(non_existing_module),
    ok.

code_which_wrong_argument(Argument) ->
    try code:which(Argument) of
        _ -> not_badarg
    catch
        error:badarg -> ok;
        _:_ -> not_badarg
    end.

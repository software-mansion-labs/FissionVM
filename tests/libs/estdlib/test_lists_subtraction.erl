%
% This file is part of AtomVM.
%
% Copyright 2024 Tomasz Sobkiewicz <tomasz.sobkiewicz@swmansion.com>
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
-module(test_lists_subtraction).

-export([test/0, start/0]).

start() ->
    test().

test() ->
    Tests = [
        {[], [], []},
        {[1, 2, 2, 3], [], [1, 2, 2, 3]},
        {[1, 2, 3], [2, 2], [1, 3]},
        {[1, 2, 3], [1,2,3], []},
        {[1, 2, 3, 2, 1], [2], [1, 3, 2, 1]},
        {[1, 2, 3, 4, 5], [2, 5], [1, 3, 4]},
        {["a", "b", "c"], ["b", "d"], ["a", "c"]},
        {["apple", "banana", "cherry"], ["banana", "lemon"], ["apple", "cherry"]}
    ],

    run_tests(Tests).

run_tests([]) ->
    ok;
run_tests([{List1, List2, Expected} | T]) ->
    Result = List1 -- List2,
    assert_list_equal(Expected, Result),
    run_tests(T).

assert_list_equal(Expected, Actual) ->
    true = Expected == Actual,
    ok.

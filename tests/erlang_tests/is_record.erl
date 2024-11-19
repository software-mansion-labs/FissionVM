%
% This file is part of AtomVM.
%
% Copyright 2024 Tomasz Sobkiewicz <tomasz.sobkiewt>
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

-module(is_record).

-export([start/0, get_person/0, get_foo/0]).

start() ->
    ok = test_is_record(),
    ok.

get_person() ->
    person.

get_foo() -> foo.

-record(person, {id, name, age}).

test_is_record() ->
    Person = ?MODULE:get_person(),
    Foo = ?MODULE:get_foo(),
    Mike = #person{id = 1, name = "Mike", age = 32},
    true = erlang:is_record({Person, 1, 2, 3, 4}, Person),
    true = erlang:is_record({person}, Person),
    true = erlang:is_record(Mike, Person),
    false = erlang:is_record(Mike, Foo),
    false = erlang:is_record([], Person),
    false = erlang:is_record({person, 1, 2, 3}, Foo),
    ok.

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

-export([start/0]).

start() ->
    ok = test_is_record(),
    ok.


test_is_record() ->
    Person = person,
    Foo = foo,
    true = erlang:is_record({Person , 1 , 2 ,3 ,4}, Person),
    true = erlang:is_record({person}, Person),
    false = erlang:is_record([], Person),
    false = erlang:is_record({person, 1 ,2 ,3}, Foo),
    ok.


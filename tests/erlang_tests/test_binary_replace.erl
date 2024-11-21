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

-module(test_binary_replace).

-export([start/0]).

start() ->
    ok = test_basic_replace(),
    ok = test_global_replace(),
    0.

test_basic_replace() ->
    <<"barbar">> = binary:replace(<<"foobar">>, <<"foo">>, <<"bar">>),
    <<"foooobar">> = binary:replace(<<"foobar">>, <<"o">>, <<"ooo">>),
    <<"">> = binary:replace(<<"foobar">>, <<"foobar">>, <<"">>),
    <<"foobar">> = binary:replace(<<"o">>, <<"o">>, <<"foobar">>),
    <<"fof">> = binary:replace(<<"foobar">>, <<"obar">>, <<"f">>),
    <<"fobar">> = binary:replace(<<"foobar">>, <<"oo">>, <<"o">>),
    <<"o">> = binary:replace(<<"o">>, <<"foobar">>, <<"o">>),
    <<"foobar">> = binary:replace(<<"o">>, <<"o">>, <<"foobar">>),
    <<"fobar">> = binary:replace(<<"foobar">>, <<"oo">>, <<"o">>, []),
    ok.

test_global_replace() ->
    <<"foobar">> = binary:replace(<<"foooobar">>, <<"oo">>, <<"o">>, [global]),
    <<"foooobar">> = binary:replace(<<"foobar">>, <<"o">>, <<"oo">>, [global]),
    <<"">> = binary:replace(<<"foofoo">>, <<"foo">>, <<"">>, [global]),
    <<"foofoo">> = binary:replace(<<"oo">>, <<"o">>, <<"foo">>, [global]),
    ok.

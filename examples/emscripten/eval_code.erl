%
% This file is part of AtomVM.
%
% Copyright 2023 Mateusz Front <mateusz.front@swmansion.com>
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
-module(eval_code).
-export([start/0]).

start() ->
    % Register as main
    register(main, self()),
    loop().

eval(String) ->
    try
        {ok, Ts, _} = erl_scan:string(String),
        {ok, Exprs} = erl_parse:parse_exprs(Ts),
        erl_eval:exprs(Exprs, [])
    catch
        _:Error:Stacktrace -> {error, Error, Stacktrace}
    end.

loop() ->
    receive
        {emscripten, {call, Promise, Data}} ->
            Result =
                case eval(binary_to_list(Data)) of
                    {value, Value, _} -> Value;
                    Other -> Other
                end,
            ResultStr = io_lib:format("~p", [Result]),
            emscripten:promise_resolve(Promise, ResultStr),
            ok
    end,
    loop().

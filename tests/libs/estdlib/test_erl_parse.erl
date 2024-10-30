%
% This file is part of AtomVM.
%
% Copyright 2024 Jakub Gonet <jakub.gonet@swmansion.com>
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
-module(test_erl_parse).

-export([test/0]).

test() ->
    {Fn, ExpectedFn} =
        {"fun(X) -> X + 1 end.", [
            {'fun', 1,
                {clauses, [
                    {clause, 1, [{var, 1, 'X'}], [], [{op, 1, '+', {var, 1, 'X'}, {integer, 1, 1}}]}
                ]}}
        ]},

    Comment = "% A comment",
    {Assignment, ExpectedAssignment} =
        {"A = 5,\nB = 10.", [
            {match, 1, {var, 1, 'A'}, {integer, 1, 5}},
            {match, 2, {var, 2, 'B'}, {integer, 2, 10}}
        ]},
    {ok, ExpectedFn} = parse(Fn),
    {error, _CommentErrorReason} = parse(Comment),
    {ok, ExpectedAssignment} = parse(Assignment),
    ok.

parse(String) ->
    try
        {ok, Ts, _} = erl_scan:string(String),
        erl_parse:parse_exprs(Ts)
    catch
        _:FullError ->
            {badmatch, Error} = FullError,
            Error
    end.

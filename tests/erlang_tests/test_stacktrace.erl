%
% This file is part of AtomVM.
%
% Copyright 2022 Fred Dushin <fred@dushin.net>
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
-module(test_stacktrace).

-export([start/0, maybe_crash/1]).

start() ->
    ok = load_crappy_module(),
    ok = test_local_throw(),
    ok = test_local_error(),
    ok = test_badmatch(),
    ok = test_apply(),
    ok = test_fun(),
    ok = test_remote_throw(),
    ok = test_tail_recursive_throw(),
    ok = test_body_recursive_throw(),
    ok = test_spawned_throw(),
    ok = test_catch(),
    ok = maybe_test_filelineno(),
    0.

load_crappy_module() ->
    % The module below is modified to contain 3 filenames (foo.erl, file1.erl and file2.erl)
    % and line numbers >= 2048. Both of these features make it trigger specific code paths
    % in parsing the BEAM LINE chunk. Apart from that, the module code is the following:
    %
    % -module(crappy_module).
    % -export([make_error/1, bar/0]).
    % make_error(Term) -> error(Term), error.
    % bar() -> ok.

    CrappyModuleBeam =
        <<70, 79, 82, 49, 0, 0, 2, 52, 66, 69, 65, 77, 65, 116, 85, 56, 0, 0, 0, 77, 0, 0, 0, 8, 13,
            99, 114, 97, 112, 112, 121, 95, 109, 111, 100, 117, 108, 101, 10, 109, 97, 107, 101, 95,
            101, 114, 114, 111, 114, 6, 101, 114, 108, 97, 110, 103, 5, 101, 114, 114, 111, 114, 3,
            102, 111, 111, 2, 111, 107, 11, 109, 111, 100, 117, 108, 101, 95, 105, 110, 102, 111,
            15, 103, 101, 116, 95, 109, 111, 100, 117, 108, 101, 95, 105, 110, 102, 111, 0, 0, 0,
            67, 111, 100, 101, 0, 0, 0, 85, 0, 0, 0, 16, 0, 0, 0, 0, 0, 0, 0, 169, 0, 0, 0, 9, 0, 0,
            0, 4, 1, 16, 153, 16, 2, 18, 34, 16, 1, 32, 153, 32, 78, 16, 0, 1, 48, 153, 48, 2, 18,
            82, 0, 1, 64, 64, 98, 3, 19, 1, 80, 153, 0, 2, 18, 114, 0, 1, 96, 64, 18, 3, 78, 16, 16,
            1, 112, 153, 0, 2, 18, 114, 16, 1, 128, 64, 3, 19, 64, 18, 3, 78, 32, 32, 3, 0, 0, 0,
            83, 116, 114, 84, 0, 0, 0, 0, 73, 109, 112, 84, 0, 0, 0, 40, 0, 0, 0, 3, 0, 0, 0, 3, 0,
            0, 0, 4, 0, 0, 0, 1, 0, 0, 0, 3, 0, 0, 0, 8, 0, 0, 0, 1, 0, 0, 0, 3, 0, 0, 0, 8, 0, 0,
            0, 2, 69, 120, 112, 84, 0, 0, 0, 52, 0, 0, 0, 4, 0, 0, 0, 7, 0, 0, 0, 1, 0, 0, 0, 8, 0,
            0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 6, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 2, 0, 0,
            0, 1, 0, 0, 0, 2, 76, 111, 99, 84, 0, 0, 0, 4, 0, 0, 0, 0, 65, 116, 116, 114, 0, 0, 0,
            39, 131, 108, 0, 0, 0, 1, 104, 2, 119, 3, 118, 115, 110, 108, 0, 0, 0, 1, 110, 16, 0,
            86, 218, 176, 105, 192, 66, 90, 202, 116, 113, 109, 86, 199, 94, 207, 115, 106, 106, 0,
            67, 73, 110, 102, 0, 0, 0, 54, 131, 108, 0, 0, 0, 2, 104, 2, 119, 7, 118, 101, 114, 115,
            105, 111, 110, 107, 0, 5, 56, 46, 51, 46, 50, 104, 2, 119, 7, 111, 112, 116, 105, 111,
            110, 115, 108, 0, 0, 0, 1, 119, 9, 102, 114, 111, 109, 95, 99, 111, 114, 101, 106, 106,
            0, 0, 68, 98, 103, 105, 0, 0, 0, 46, 131, 104, 3, 119, 13, 100, 101, 98, 117, 103, 95,
            105, 110, 102, 111, 95, 118, 49, 119, 17, 101, 114, 108, 95, 97, 98, 115, 116, 114, 97,
            99, 116, 95, 99, 111, 100, 101, 104, 2, 119, 4, 110, 111, 110, 101, 106, 0, 0, 76, 105,
            110, 101, 0, 0, 0, 51, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, 3, 0, 0, 0, 2, 18,
            25, 8, 88, 25, 8, 89, 34, 145, 0, 9, 102, 105, 108, 101, 49, 46, 101, 114, 108, 0, 9,
            102, 105, 108, 101, 50, 46, 101, 114, 108, 0, 84, 121, 112, 101, 0, 0, 0, 10, 0, 0, 0,
            2, 0, 0, 0, 1, 31, 255, 0, 0>>,
    {module, crappy_module} = code:load_binary(crappy_module, "", CrappyModuleBeam),
    ok.

test_local_throw() ->
    ok =
        try
            maybe_crash(throw_me),
            fail
        catch
            throw:throw_me:Stacktrace ->
                expect_stacktrace(
                    Stacktrace,
                    [
                        {?MODULE, maybe_crash, 1},
                        {?MODULE, test_local_throw, 0},
                        {?MODULE, start, 0}
                    ]
                )
        end.

test_local_error() ->
    ok =
        try
            maybe_crash(error_me),
            fail
        catch
            error:error_me:Stacktrace ->
                expect_stacktrace(
                    Stacktrace,
                    [
                        {crappy_module, make_error, 1, [{file, "file1.erl"}, {line, 2137}]},
                        {?MODULE, test_local_error, 0},
                        {?MODULE, start, 0}
                    ]
                )
        end.

test_badmatch() ->
    ok =
        try
            maybe_badmatch(crash_me),
            fail
        catch
            error:{badmatch, crash_me}:Stacktrace ->
                expect_stacktrace(
                    Stacktrace,
                    [
                        {?MODULE, maybe_badmatch, 1},
                        {?MODULE, test_badmatch, 0},
                        {?MODULE, start, 0}
                    ]
                )
        end.

test_remote_throw() ->
    ok =
        try
            ?MODULE:maybe_crash(throw_me),
            fail
        catch
            throw:throw_me:Stacktrace ->
                expect_stacktrace(
                    Stacktrace,
                    [
                        {?MODULE, maybe_crash, 1},
                        {?MODULE, test_remote_throw, 0},
                        {?MODULE, start, 0}
                    ]
                )
        end.

test_apply() ->
    ok =
        try
            erlang:apply(?MODULE, maybe_crash, [throw_me]),
            fail
        catch
            throw:throw_me:Stacktrace ->
                expect_stacktrace(
                    Stacktrace,
                    [
                        {?MODULE, maybe_crash, 1},
                        {?MODULE, test_apply, 0},
                        {?MODULE, start, 0}
                    ]
                )
        end.

test_fun() ->
    ok =
        try
            F = fun() -> maybe_crash(throw_me) end,
            F(),
            fail
        catch
            throw:throw_me:Stacktrace ->
                expect_stacktrace(
                    Stacktrace,
                    [
                        {?MODULE, maybe_crash, 1},
                        {?MODULE, test_fun, 0},
                        {?MODULE, start, 0}
                    ]
                )
        end.

a_tail_recursive_function(0, Msg) ->
    maybe_crash(Msg);
a_tail_recursive_function(I, Msg) ->
    a_tail_recursive_function(I - 1, Msg).

test_tail_recursive_throw() ->
    ok =
        try
            a_tail_recursive_function(5, throw_me),
            fail
        catch
            throw:throw_me:Stacktrace ->
                expect_stacktrace(
                    Stacktrace,
                    [
                        {?MODULE, maybe_crash, 1},
                        {?MODULE, test_tail_recursive_throw, 0},
                        {?MODULE, start, 0}
                    ]
                )
        end.

a_body_recursive_function(0, _Msg) ->
    ok;
a_body_recursive_function(I, Msg) ->
    case a_body_recursive_function(I - 1, Msg) of
        ok ->
            maybe_crash(Msg);
        _ ->
            error
    end.

test_body_recursive_throw() ->
    ok =
        try
            a_body_recursive_function(5, throw_me),
            fail
        catch
            throw:throw_me:Stacktrace ->
                expect_stacktrace(
                    Stacktrace,
                    [
                        {?MODULE, maybe_crash, 1},
                        {?MODULE, a_body_recursive_function, 2},
                        {?MODULE, test_body_recursive_throw, 0},
                        {?MODULE, start, 0}
                    ]
                )
        end.

test_spawned_throw() ->
    Self = self(),
    spawn_opt(
        fun() ->
            try
                do_some_stuff(blah),
                a_tail_recursive_function(5, throw_me),
                do_some_stuff(blah),
                Self ! fail
            catch
                throw:throw_me:Stacktrace ->
                    Result = expect_stacktrace(
                        Stacktrace,
                        [
                            {?MODULE, maybe_crash, 1},
                            {?MODULE, '-test_spawned_throw/0-fun-0-', 1}
                        ]
                    ),
                    Self ! Result
            end
        end,
        []
    ),
    receive
        Result ->
            erlang:display(Result),
            Result
    end.

test_catch() ->
    {'EXIT', {error_me, Stacktrace}} = (catch maybe_crash(error_me)),
    Result = expect_stacktrace(
        Stacktrace,
        [
            {crappy_module, make_error, 1, [{file, "file1.erl"}, {line, 2137}]},
            {?MODULE, test_catch, 0},
            {?MODULE, start, 0}
        ]
    ),
    do_some_stuff(Result),
    Result.

maybe_test_filelineno() ->
    ok =
        try
            throw_with_file_and_line(),
            fail
        catch
            throw:{File, Line}:Stacktrace ->
                [Frame | _] = Stacktrace,
                {?MODULE, throw_with_file_and_line, 0, AuxData} = Frame,
                case {get_value(file, AuxData), get_value(line, AuxData)} of
                    {undefined, undefined} ->
                        ok;
                    {F, L} ->
                        Ef =
                            case is_binary(F) of
                                true ->
                                    erlang:binary_to_list(F);
                                _ ->
                                    F
                            end,
                        case File == Ef andalso Line == L of
                            true ->
                                ok;
                            _ ->
                                {unexpected_file_line, F, L}
                        end
                end
        end.

get_value(_Key, []) ->
    undefined;
get_value(Key, [{Key, Value} | _]) ->
    Value;
get_value(Key, [_ | T]) ->
    get_value(Key, T).

throw_with_file_and_line() ->
    throw({?FILE, ?LINE}).

maybe_crash(Term) ->
    case Term of
        ok ->
            ok;
        throw_me ->
            throw(Term);
        error_me ->
            crappy_module:make_error(Term)
    end.

maybe_badmatch(Term) ->
    ok = Term.

do_some_stuff(_) ->
    ok.

expect_stacktrace(Stacktrace, Expect) ->
    case validate_stacktrace(Stacktrace) of
        ok -> ok;
        Error1 -> error(Error1)
    end,
    case contains_in_order(Stacktrace, Expect) of
        true -> ok;
        Error2 -> error(Error2)
    end.

validate_stacktrace([]) ->
    ok;
validate_stacktrace([{M, F, A, [{file, File}, {line, Line}]} | Stacktrace]) when
    is_atom(M) and is_atom(F) and is_integer(A) and
        (A >= 0) and is_list(File) and is_integer(Line) and (Line >= 0)
->
    validate_stacktrace(Stacktrace);
validate_stacktrace([E | _S]) ->
    {invalid_stacktrace_entry, E}.

contains_in_order(_Stacktrace, []) ->
    true;
contains_in_order(Stacktrace, [H | Expect]) ->
    case find(Stacktrace, H) of
        not_found ->
            {entry_not_found, H, Stacktrace};
        Stacktrace2 ->
            contains_in_order(Stacktrace2, Expect)
    end.

find([], _Entry) ->
    not_found;
find([Entry | Stacktrace], Entry) ->
    Stacktrace;
find([{M, F, A, _Location} | Stacktrace], {M, F, A}) ->
    Stacktrace;
find([_H | Stacktrace], Entry) ->
    find(Stacktrace, Entry).

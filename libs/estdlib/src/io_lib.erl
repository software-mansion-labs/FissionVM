%
% This file is part of AtomVM.
%
% Copyright 2019-2022 Fred Dushin <fred@dushin.net>
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

%%-----------------------------------------------------------------------------
%% @doc An implementation of the Erlang/OTP io_lib interface.
%%
%% This module implements a strict subset of the Erlang/OTP io_lib
%% interface.
%% @end
%%-----------------------------------------------------------------------------
-module(io_lib).

-export([
    format/2,
    fwrite/2,
    latin1_char_list/1,
    write_atom/1,
    write_atom_as_latin1/1,
    deep_char_list/1,
    write/1, write/2, write/3,
    write_binary/3,
    write_string/1,
    quote_atom/2,
    write_char/1
]).
-type chars() :: [char() | chars()].
-type depth() :: -1 | non_neg_integer().
-type chars_limit() :: integer().
-type latin1_string() :: [unicode:latin1_char()].

-spec fwrite(Format, Data) -> chars() when
    Format :: io:format(),
    Data :: [term()].

fwrite(Format, Args) when is_binary(Format) ->
    format(binary_to_list(Format), Args);
fwrite(Format, Args) ->
    format(Format, Args).

%%-----------------------------------------------------------------------------
%% @param   Format format string
%% @param   Args format argument
%% @returns string
%% @doc     Format string and data to a string.
%%          Approximates features of OTP io_lib:format/2, but
%%          only supports ~p and ~n format specifiers.
%%          Raises `badarg' error if the number of format specifiers
%%          does not match the length of the Args.
%% @end
%%-----------------------------------------------------------------------------
-spec format(Format :: string(), Args :: list()) -> string().
format(Format, Args) ->
    {FormatTokens, Instr} = split(Format),
    case length(FormatTokens) == length(Args) + 1 of
        true ->
            interleave(FormatTokens, Instr, Args, []);
        false ->
            error(badarg)
    end.

%%-----------------------------------------------------------------------------
%% @param   Term term to test
%% @returns true if Term is a list of latin1 characters, false otherwise.
%% @doc     Determine if passed term is a list of ISO-8859-1 characters (0-255).
%% @end
%%-----------------------------------------------------------------------------
-spec latin1_char_list(Term :: any()) -> boolean().
latin1_char_list([H | T]) when is_integer(H) andalso H >= 0 andalso H =< 255 ->
    latin1_char_list(T);
latin1_char_list([]) ->
    true;
latin1_char_list(_) ->
    false.

%%
%% internal operations
%%

-record(format, {
    field_width = undefined :: number() | undefined,
    precision = undefined :: number() | undefined,
    pad = undefined :: char() | undefined,
    mod = undefined :: atom() | undefined,
    control :: atom() | undefined
}).

%% @private
split(Format) ->
    split(Format, [], [], []).

%% @private
split([], Cur, Accum, Instr) ->
    {lists:reverse([lists:reverse(Cur) | Accum]), lists:reverse(Instr)};
split([$~ | Tail], Cur, Accum, Instr) ->
    {FormatSpec, Rest} = parse_format(Tail),
    case FormatSpec of
        {literal, Lit} ->
            split(Rest, [Lit | Cur], Accum, Instr);
        Format = #format{} ->
            split(Rest, [], [lists:reverse(Cur) | Accum], [
                fun(T) -> format_term(Format, T) end | Instr
            ]);
        ignore ->
            split(Rest, [], [lists:reverse(Cur) | Accum], [fun(_T) -> [] end | Instr])
    end;
split([Char | Rest], Cur, Accum, Instr) ->
    split(Rest, [Char | Cur], Accum, Instr).

%% @private
parse_format([$i | Rest]) ->
    {ignore, Rest};
parse_format([$~ | Rest]) ->
    {{literal, $~}, Rest};
parse_format([$n | Rest]) ->
    {{literal, $\n}, Rest};
parse_format(String) ->
    Format = #format{},
    parse_format_field_width(String, Format).

%% @private
parse_format_field_width([$. | Rest], Format) ->
    parse_format_precision(Rest, Format);
parse_format_field_width([C | _] = String, Format0) when
    C =:= $- orelse (C >= $0 andalso C =< $9)
->
    {Value, Rest0} = parse_integer(String),
    Format1 = Format0#format{field_width = Value},
    case Rest0 of
        [$. | Rest1] ->
            parse_format_precision(Rest1, Format1);
        _ ->
            parse_format_mod(Rest0, Format1)
    end;
parse_format_field_width(String, Format) ->
    parse_format_mod(String, Format).

%% @private
parse_format_precision([$. | Rest], Format) ->
    parse_format_pad(Rest, Format);
parse_format_precision([C | _] = String, Format) when C =:= $- orelse (C >= $0 andalso C =< $9) ->
    {Value, Rest} = parse_integer(String),
    parse_format_precision(Rest, Format#format{precision = Value});
parse_format_precision(String, Format) ->
    parse_format_mod(String, Format).

%% @private
parse_format_pad([Pad | Rest], Format) ->
    parse_format_mod(Rest, Format#format{pad = Pad}).

%% @private
parse_format_mod([$t | Rest], Format) ->
    parse_format_control(Rest, Format#format{mod = t});
parse_format_mod([$l | Rest], Format) ->
    parse_format_control(Rest, Format#format{mod = l});
parse_format_mod([$k | Rest], Format) ->
    parse_format_control(Rest, Format#format{mod = k});
parse_format_mod(String, Format) ->
    parse_format_control(String, Format).

%% @private
parse_format_control([$s | Rest], Format) -> {Format#format{control = s}, Rest};
parse_format_control([$p | Rest], Format) -> {Format#format{control = p}, Rest};
parse_format_control([$w | Rest], Format) -> {Format#format{control = w}, Rest};
parse_format_control([$c | Rest], Format) -> {Format#format{control = c}, Rest};
parse_format_control([$B | Rest], Format) -> {Format#format{control = 'B'}, Rest};
parse_format_control([$b | Rest], Format) -> {Format#format{control = b}, Rest};
parse_format_control([$# | Rest], Format) -> {Format#format{control = '#'}, Rest};
parse_format_control([$+ | Rest], Format) -> {Format#format{control = '+'}, Rest};
parse_format_control([$e | Rest], Format) -> {Format#format{control = e}, Rest};
parse_format_control([$f | Rest], Format) -> {Format#format{control = f}, Rest};
parse_format_control([$g | Rest], Format) -> {Format#format{control = g}, Rest};
parse_format_control(_String, _Format) -> error({badarg, _String}).

%% @private
parse_integer([$- | Tail]) ->
    {Val, Rest} = parse_integer0(Tail, 0),
    {-Val, Rest};
parse_integer(Str) ->
    parse_integer0(Str, 0).

%% @private
parse_integer0([C | Tail], Acc) when C >= $0 andalso C =< $9 ->
    parse_integer0(Tail, Acc * 10 + C - $0);
parse_integer0(Str, Acc) ->
    {Acc, Str}.

%% @private
interleave([LastToken], _Instr, [], Accum) ->
    lists:reverse([LastToken | Accum]);
interleave([Token | Tokens], [Formatter | Instr], [Arg | Args], Accum) ->
    interleave(Tokens, Instr, Args, [Formatter(Arg), Token | Accum]).

%% @private
format_term(#format{control = C} = Format, T) when C =:= s orelse C =:= p orelse C =:= w ->
    trunc_or_pad(Format, format_string(Format, T));
format_term(#format{control = C} = Format, T) when
    C =:= 'B' orelse C =:= b orelse C =:= '#' orelse C =:= '+'
->
    trunc_or_pad(Format, format_integer(Format, T));
format_term(#format{control = C} = Format, T) when C =:= e orelse C =:= f orelse C =:= g ->
    trunc_or_pad(Format, format_float(Format, T));
format_term(#format{control = 'c'} = Format, T) ->
    trunc_or_pad(Format, format_char(Format, T)).

%% @private
trunc_or_pad(#format{field_width = undefined, pad = undefined}, Str) ->
    Str;
trunc_or_pad(#format{field_width = Width, pad = Pad0}, Str0) when Width =/= undefined ->
    Str = lists:flatten(Str0),
    AbsWidth = abs(Width),
    Pad =
        case Pad0 of
            undefined -> 32;
            _ -> Pad0
        end,
    Len = length(Str),
    if
        Len > AbsWidth ->
            lists:duplicate(AbsWidth, $*);
        Len =:= AbsWidth ->
            Str;
        Width < 0 ->
            [Str, lists:duplicate(AbsWidth - Len, Pad)];
        true ->
            [lists:duplicate(AbsWidth - Len, Pad), Str]
    end.

%% @private
format_string(
    #format{control = s, precision = Precision, field_width = FieldWidth} = Format, T
) when Precision =/= undefined orelse FieldWidth =/= undefined ->
    Str0 = format_spw(Format, T),
    Str = lists:flatten(Str0),
    TruncSize =
        if
            Precision =:= undefined -> abs(FieldWidth);
            true -> Precision
        end,
    lists:sublist(Str, TruncSize);
format_string(Format, T) ->
    format_spw(Format, T).

%% @private
format_spw(#format{control = s}, T) when is_atom(T) ->
    erlang:atom_to_list(T);
format_spw(_Format, T) when is_atom(T) ->
    AtomStr = erlang:atom_to_list(T),
    case atom_requires_quotes(T, AtomStr) of
        false -> AtomStr;
        true -> [$', AtomStr, $']
    end;
format_spw(#format{control = s, mod = t} = Format, T) when is_binary(T) ->
    case unicode:characters_to_list(T, utf8) of
        L when is_list(L) -> L;
        E when is_tuple(E) ->
            format_spw(Format#format{mod = undefined}, T)
    end;
format_spw(#format{control = s, mod = undefined}, T) when is_binary(T) ->
    erlang:binary_to_list(T);
format_spw(#format{control = Control, mod = t} = Format, T) when is_binary(T) ->
    case unicode:characters_to_list(T, utf8) of
        L when is_list(L) ->
            FormattedStr =
                case {Control, test_string_class(L)} of
                    {p, latin1_printable} -> format_p_string(L, []);
                    {p, unicode} -> [format_p_string(L, []), "/utf8"];
                    _ -> lists:join($,, [integer_to_list(B) || B <- L])
                end,
            [$<, $<, FormattedStr, $>, $>];
        E when is_tuple(E) ->
            format_spw(Format#format{mod = undefined}, T)
    end;
format_spw(#format{control = Control, mod = undefined}, T) when is_binary(T) ->
    L = erlang:binary_to_list(T),
    FormattedStr =
        case {Control, test_string_class(L)} of
            {p, latin1_printable} -> format_p_string(L, []);
            _ -> lists:join($,, [integer_to_list(B) || B <- L])
        end,
    [$<, $<, FormattedStr, $>, $>];
format_spw(#format{control = s, mod = Mod}, L) when is_list(L) ->
    Flatten = lists:flatten(L),
    case {Mod, test_string_class(Flatten)} of
        {_, not_a_string} -> error(badarg);
        {undefined, unicode} -> error(badarg);
        {_, _} -> Flatten
    end;
format_spw(#format{control = p} = Format, L) when is_list(L) ->
    case test_string_class(L) of
        latin1_printable -> format_p_string(L, []);
        _ -> [$[, lists:join($,, [format_spw(Format, E) || E <- L]), $]]
    end;
format_spw(#format{control = w} = Format, L) when is_list(L) ->
    [$[, lists:join($,, [format_spw(Format, E) || E <- L]), $]];
format_spw(#format{control = s}, _) ->
    error(badarg);
format_spw(_Format, T) when is_integer(T) ->
    erlang:integer_to_list(T);
format_spw(_Format, T) when is_float(T) ->
    erlang:float_to_list(T);
format_spw(_Format, T) when is_pid(T) ->
    erlang:pid_to_list(T);
format_spw(_Format, T) when is_reference(T) ->
    erlang:ref_to_list(T);
format_spw(_Format, T) when is_function(T) ->
    erlang:fun_to_list(T);
format_spw(Format, T) when is_tuple(T) ->
    [${, lists:join($,, [format_spw(Format, E) || E <- tuple_to_list(T)]), $}];
format_spw(#format{mod = Mod} = Format, T) when is_map(T) ->
    Order =
        case Mod of
            undefined -> undefined;
            k -> ordered
        end,
    [
        $#,
        ${,
        lists:join($,, [
            [format_spw(Format, K), " => ", format_spw(Format, V)]
         || {K, V} <- maps:to_list(maps:iterator(T, Order))
        ]),
        $}
    ].

%% We will probably need to add 'maybe' with OTP 27
-define(RESERVED_KEYWORDS, [
    'after',
    'and',
    'andalso',
    'band',
    'begin',
    'bnot',
    'bor',
    'bsl',
    'bsr',
    'bxor',
    'case',
    'catch',
    'cond',
    'div',
    'end',
    'fun',
    'if',
    'let',
    'not',
    'of',
    'or',
    'orelse',
    'receive',
    'rem',
    'try',
    'when',
    'xor'
]).

%% @private
atom_requires_quotes(Atom, AtomStr) ->
    case lists:member(Atom, ?RESERVED_KEYWORDS) of
        true -> true;
        false -> atom_requires_quotes0(AtomStr)
    end.

atom_requires_quotes0([C | _T]) when C < $a orelse C > $z -> true;
atom_requires_quotes0([_C | T]) -> atom_requires_quotes1(T).

atom_requires_quotes1([]) -> false;
atom_requires_quotes1([$@ | T]) -> atom_requires_quotes1(T);
atom_requires_quotes1([$_ | T]) -> atom_requires_quotes1(T);
atom_requires_quotes1([C | T]) when C >= $A andalso C =< $Z -> atom_requires_quotes1(T);
atom_requires_quotes1([C | T]) when C >= $0 andalso C =< $9 -> atom_requires_quotes1(T);
atom_requires_quotes1([C | T]) when C >= $a andalso C =< $z -> atom_requires_quotes1(T);
atom_requires_quotes1(_) -> true.

%% @private
format_integer(#format{control = C, precision = Precision0}, T0) when
    is_integer(T0) andalso (C =:= '#' orelse C =:= '+')
->
    Base =
        case Precision0 of
            undefined -> 10;
            _ -> Precision0
        end,
    {Sign, T} =
        if
            T0 < 0 ->
                {"-", -T0};
            true ->
                {[], T0}
        end,
    Str0 = integer_to_list(T, Base),
    Str =
        case C of
            '#' -> Str0;
            '+' -> string:to_lower(Str0)
        end,
    [Sign, integer_to_list(Base), "#", Str];
format_integer(#format{precision = undefined}, T) when is_integer(T) ->
    integer_to_list(T);
format_integer(#format{control = 'B', precision = Base}, T) when is_integer(T) ->
    integer_to_list(T, Base);
format_integer(#format{control = b, precision = Base}, T) when is_integer(T) ->
    string:to_lower(integer_to_list(T, Base));
format_integer(_Format, _) ->
    error(badarg).

%% @private
format_float(#format{control = f, precision = undefined}, T) when is_float(T) ->
    float_to_list(T, [{decimals, 6}]);
format_float(#format{control = f, precision = Precision}, T) when is_float(T) ->
    float_to_list(T, [{decimals, Precision}]);
format_float(#format{control = g, precision = undefined}, T) when
    is_float(T) andalso T >= 0.1 andalso T < 10000.0
->
    float_to_list(T, [{decimals, 5}]);
format_float(#format{control = g, precision = Precision}, T) when
    is_float(T) andalso T >= 0.1 andalso T < 10000.0
->
    float_to_list(T, [{decimals, Precision - 1}]);
format_float(#format{control = C, precision = undefined}, T) when
    is_float(T) andalso (C =:= e orelse C =:= g)
->
    format_scientific(T, 6, 0);
format_float(#format{control = C, precision = Precision}, T) when
    is_float(T) andalso (C =:= e orelse C =:= g)
->
    format_scientific(T, Precision, 0);
format_float(_Format, _) ->
    error(badarg).

%% @private
format_scientific(T, Precision, E) when (T < 1 andalso T > 0) orelse (T > -1 andalso T < 0) ->
    format_scientific(T * 10, Precision, E - 1);
format_scientific(T, Precision, E) when T >= 10 orelse T =< -10 ->
    format_scientific(T / 10, Precision, E + 1);
format_scientific(T, Precision, E) when E >= 0 ->
    [float_to_list(T, [{decimals, Precision - 1}]), "e+", integer_to_list(E)];
format_scientific(T, Precision, E) ->
    [float_to_list(T, [{decimals, Precision - 1}]), "e", integer_to_list(E)].

%% @private
format_char(#format{field_width = FieldWidth, precision = undefined} = Format, T) when
    FieldWidth =/= undefined
->
    format_char(Format#format{field_width = undefined, precision = FieldWidth}, T);
format_char(#format{mod = undefined, precision = undefined}, T) when is_integer(T) ->
    [T band 16#FF];
% TODO: check T is valid unicode char
format_char(#format{mod = t, precision = undefined}, T) when is_integer(T) -> [T];
format_char(#format{precision = Precision} = Format, T) when Precision =/= undefined ->
    [Ch] = format_char(Format#format{field_width = undefined, precision = undefined}, T),
    lists:duplicate(Precision, Ch);
format_char(_, _) ->
    error(badarg).
-spec write_atom(Atom) -> chars() when
    Atom :: atom().

write_atom(Atom) ->
    write_possibly_quoted_atom(Atom, fun write_string/2).

-spec write_atom_as_latin1(Atom) -> latin1_string() when
    Atom :: atom().

write_atom_as_latin1(Atom) ->
    write_possibly_quoted_atom(Atom, fun write_string_as_latin1/2).

write_possibly_quoted_atom(Atom, PFun) ->
    Chars = atom_to_list(Atom),
    case quote_atom(Atom, Chars) of
        true ->
            %'
            PFun(Chars, $');
        false ->
            Chars
    end.

%% quote_atom(Atom, CharList)
%%  Return 'true' if atom with chars in CharList needs to be quoted, else
%%  return 'false'. Notice that characters >= 160 are always quoted.

-spec quote_atom(atom(), chars()) -> boolean().

quote_atom(Atom, Cs0) ->
    case erl_scan:reserved_word(Atom) of
	true -> true;
	false ->
	    case Cs0 of
		[C|Cs] when is_integer(C), C >= $a, C =< $z ->
		    not name_chars(Cs);
		[C|Cs] when is_integer(C), C >= $ß, C =< $ÿ, C =/= $÷ ->
		    not name_chars(Cs);
		[C|_] when is_integer(C) -> true;
                [] -> true
	    end
    end.

name_chars([C | Cs]) when is_integer(C) ->
    case name_char(C) of
        true -> name_chars(Cs);
        false -> false
    end;
name_chars([]) ->
    true.

name_char(C) when C >= $a, C =< $z -> true;
name_char(C) when C >= $ß, C =< $ÿ, C =/= $÷ -> true;
name_char(C) when C >= $A, C =< $Z -> true;
name_char(C) when C >= $À, C =< $Þ, C =/= $× -> true;
name_char(C) when C >= $0, C =< $9 -> true;
name_char($_) -> true;
name_char($@) -> true;
name_char(_) -> false.

%%% There are two functions to write Unicode strings:
%%% - they both escape control characters < 160;
%%% - write_string() never escapes characters >= 160;
%%% - write_string_as_latin1() also escapes characters >= 255.

%% write_string([Char]) -> [Char]
%%  Generate the list of characters needed to print a string.

-spec write_string(string(), char()) -> chars().
write_string(S, Q) ->
    [Q | write_string1(unicode_as_unicode, S, Q)].

-spec write_string_as_latin1(string(), char()) -> latin1_string().
write_string_as_latin1(S, Q) ->
    [Q | write_string1(unicode_as_latin1, S, Q)].

write_string1(_, [], Q) ->
    [Q];
write_string1(Enc, [C | Cs], Q) when is_integer(C) ->
    string_char(Enc, C, Q, write_string1(Enc, Cs, Q)).

%Must check these first!
string_char(_, Q, Q, Tail) ->
    [$\\, Q | Tail];
string_char(_, $\\, _, Tail) ->
    [$\\, $\\ | Tail];
string_char(_, C, _, Tail) when C >= $\s, C =< $~ ->
    [C | Tail];
string_char(latin1, C, _, Tail) when C >= $\240, C =< $\377 ->
    [C | Tail];
string_char(unicode_as_unicode, C, _, Tail) when C >= $\240 ->
    [C | Tail];
string_char(unicode_as_latin1, C, _, Tail) when C >= $\240, C =< $\377 ->
    [C | Tail];
string_char(unicode_as_latin1, C, _, Tail) when C >= $\377 ->
    "\\x{" ++ erlang:integer_to_list(C, 16) ++ "}" ++ Tail;
%\n = LF
string_char(_, $\n, _, Tail) ->
    [$\\, $n | Tail];
%\r = CR
string_char(_, $\r, _, Tail) ->
    [$\\, $r | Tail];
%\t = TAB
string_char(_, $\t, _, Tail) ->
    [$\\, $t | Tail];
%\v = VT
string_char(_, $\v, _, Tail) ->
    [$\\, $v | Tail];
%\b = BS
string_char(_, $\b, _, Tail) ->
    [$\\, $b | Tail];
%\f = FF
string_char(_, $\f, _, Tail) ->
    [$\\, $f | Tail];
%\e = ESC
string_char(_, $\e, _, Tail) ->
    [$\\, $e | Tail];
%\d = DEL
string_char(_, $\d, _, Tail) ->
    [$\\, $d | Tail];
%Other control characters.
string_char(_, C, _, Tail) when C < $\240 ->
    C1 = (C bsr 6) + $0,
    C2 = ((C bsr 3) band 7) + $0,
    C3 = (C band 7) + $0,
    [$\\, C1, C2, C3 | Tail].

%% @private
%% String classes:
%% latin1_printable
%% latin1_unprintable
%% unicode
%% io_lib doesn't distinguish between valid unicode and invalid unicode
%% characters. This is done with io, though, when actually writing the string.
%% Compare:
%% ```
%% io_lib:format("~tc", [16#D800]).
%% io:format("~tc", [16#D800]).
%% ```
test_string_class(Str) ->
    test_string_class(Str, latin1_printable).

test_string_class([H | T], Class) when is_integer(H) andalso H >= 0 ->
    NewClass =
        case {Class, char_class(H)} of
            {_, latin1_printable} -> Class;
            {latin1_printable, CharClass} -> CharClass;
            {_, latin1_unprintable} -> Class;
            {latin1_unprintable, CharClass} -> CharClass;
            _ -> unicode
        end,
    test_string_class(T, NewClass);
test_string_class([], Class) ->
    Class;
test_string_class(_String, _Class) ->
    not_a_string.

char_class(H) when H >= 0 andalso H < 8 -> latin1_unprintable;
char_class(27) -> latin1_printable;
char_class(H) when H >= 14 andalso H < 32 -> latin1_unprintable;
char_class(H) when H < 256 -> latin1_printable;
char_class(_H) -> unicode.

-spec deep_char_list(Term) -> boolean() when
    Term :: term().

deep_char_list(Cs) ->
    deep_char_list(Cs, []).

deep_char_list([C | Cs], More) when is_list(C) ->
    deep_char_list(C, [Cs | More]);
deep_char_list([C | Cs], More) when
    is_integer(C), C >= 0, C < 16#D800;
    is_integer(C), C > 16#DFFF, C < 16#FFFE;
    is_integer(C), C > 16#FFFF, C =< 16#10FFFF
->
    deep_char_list(Cs, More);
deep_char_list([], [Cs | More]) ->
    deep_char_list(Cs, More);
deep_char_list([], []) ->
    true;
%Everything else is false
deep_char_list(_, _More) ->
    false.

-spec write(Term) -> chars() when
    Term :: term().

write(Term) ->
    write1(Term, -1, latin1, undefined).

-spec write(term(), depth(), boolean()) -> chars().

write(Term, D, true) ->
    io_lib_pretty:print(Term, 1, 80, D);
write(Term, D, false) ->
    write(Term, D).

-spec write
    (Term, Depth) -> chars() when
        Term :: term(),
        Depth :: depth();
    (Term, Options) -> chars() when
        Term :: term(),
        Options :: [Option],
        Option ::
            {'chars_limit', CharsLimit}
            | {'depth', Depth}
            | {'encoding', 'latin1' | 'utf8' | 'unicode'},
        CharsLimit :: chars_limit(),
        Depth :: depth().

write(Term, Options) when is_list(Options) ->
    Depth = get_option(depth, Options, -1),
    Encoding = get_option(encoding, Options, epp:default_encoding()),
    CharsLimit = get_option(chars_limit, Options, -1),
    MapsOrder = get_option(maps_order, Options, undefined),
    if
        Depth =:= 0; CharsLimit =:= 0 ->
            "...";
        is_integer(CharsLimit), CharsLimit < 0, is_integer(Depth) ->
            write1(Term, Depth, Encoding, MapsOrder);
        is_integer(CharsLimit), CharsLimit > 0 ->
            RecDefFun = fun(_, _) -> no end,
            If = io_lib_pretty:intermediate(
                Term, Depth, CharsLimit, RecDefFun, Encoding, _Str = false, MapsOrder
            ),
            io_lib_pretty:write(If)
    end;
write(Term, Depth) ->
    write(Term, [{depth, Depth}, {encoding, latin1}]).

write1(_Term, 0, _E, _O) ->
    "...";
write1(Term, _D, _E, _O) when is_integer(Term) -> integer_to_list(Term);
write1(Term, _D, _E, _O) when is_float(Term) -> io_lib_format:fwrite_g(Term);
write1(Atom, _D, latin1, _O) when is_atom(Atom) -> write_atom_as_latin1(Atom);
write1(Atom, _D, _E, _O) when is_atom(Atom) -> write_atom(Atom);
write1(Term, _D, _E, _O) when is_port(Term) -> write_port(Term);
write1(Term, _D, _E, _O) when is_pid(Term) -> pid_to_list(Term);
write1(Term, _D, _E, _O) when is_reference(Term) -> write_ref(Term);
write1(<<_/bitstring>> = Term, D, _E, _O) ->
    write_binary(Term, D);
write1([], _D, _E, _O) ->
    "[]";
write1({}, _D, _E, _O) ->
    "{}";
write1([H | T], D, E, O) ->
    if
        D =:= 1 -> "[...]";
        true -> [$[, [write1(H, D - 1, E, O) | write_tail(T, D - 1, E, O)], $]]
    end;
write1(F, _D, _E, _O) when is_function(F) ->
    erlang:fun_to_list(F);
write1(Term, D, E, O) when is_map(Term) ->
    write_map(Term, D, E, O);
write1(T, D, E, O) when is_tuple(T) ->
    if
        D =:= 1 ->
            "{...}";
        true ->
            [
                ${,
                [write1(element(1, T), D - 1, E, O) | write_tuple(T, 2, D - 1, E, O)],
                $}
            ]
    end.

%% write_tail(List, Depth, Encoding)
%%  Test the terminating case first as this looks better with depth.

write_tail([], _D, _E, _O) -> "";
write_tail(_, 1, _E, _O) -> [$| | "..."];
write_tail([H | T], D, E, O) -> [$,, write1(H, D - 1, E, O) | write_tail(T, D - 1, E, O)];
write_tail(Other, D, E, O) -> [$|, write1(Other, D - 1, E, O)].

write_tuple(T, I, _D, _E, _O) when I > tuple_size(T) -> "";
write_tuple(_, _I, 1, _E, _O) ->
    [$, | "..."];
write_tuple(T, I, D, E, O) ->
    [$,, write1(element(I, T), D - 1, E, O) | write_tuple(T, I + 1, D - 1, E, O)].

write_port(Port) ->
    erlang:port_to_list(Port).

write_ref(Ref) ->
    erlang:ref_to_list(Ref).

write_map(_, 1, _E, _O) ->
    "#{}";
write_map(Map, D, E, O) when is_integer(D) ->
    I = maps:iterator(Map, O),
    case maps:next(I) of
        {K, V, NextI} ->
            D0 = D - 1,
            W = write_map_assoc(K, V, D0, E, O),
            [$#, ${, [W | write_map_body(NextI, D0, D0, E, O)], $}];
        none ->
            "#{}"
    end.

write_map_body(_, 1, _D0, _E, _O) ->
    ",...";
write_map_body(I, D, D0, E, O) ->
    case maps:next(I) of
        {K, V, NextI} ->
            W = write_map_assoc(K, V, D0, E, O),
            [$,, W | write_map_body(NextI, D - 1, D0, E, O)];
        none ->
            ""
    end.

write_map_assoc(K, V, D, E, O) ->
    [write1(K, D, E, O), " => ", write1(V, D, E, O)].

write_binary(B, D) when is_integer(D) ->
    {S, _} = write_binary(B, D, -1),
    S.

write_binary(B, D, T) when is_integer(T) ->
    {S, Rest} = write_binary_body(B, D, tsub(T, 4), []),
    {[$<, $<, lists:reverse(S), $>, $>], Rest}.

write_binary_body(<<>> = B, _D, _T, Acc) ->
    {Acc, B};
write_binary_body(B, D, T, Acc) when D =:= 1; T =:= 0 ->
    {["..." | Acc], B};
write_binary_body(<<X:8>>, _D, _T, Acc) ->
    {[integer_to_list(X) | Acc], <<>>};
write_binary_body(<<X:8, Rest/bitstring>>, D, T, Acc) ->
    S = integer_to_list(X),
    write_binary_body(Rest, D - 1, tsub(T, length(S) + 1), [$,, S | Acc]);
write_binary_body(B, _D, _T, Acc) ->
    L = bit_size(B),
    <<X:L>> = B,
    {[integer_to_list(L), $:, integer_to_list(X) | Acc], <<>>}.

%% Make sure T does not change sign.
tsub(T, _) when T < 0 -> T;
tsub(T, E) when T >= E -> T - E;
tsub(_, _) -> 0.

get_option(Key, TupleList, Default) ->
    case lists:keyfind(Key, 1, TupleList) of
        false -> Default;
        {Key, Value} -> Value;
        _ -> Default
    end.

%%% There are two functions to write Unicode strings:
%%% - they both escape control characters < 160;
%%% - write_string() never escapes characters >= 160;
%%% - write_string_as_latin1() also escapes characters >= 255.

%% write_string([Char]) -> [Char]
%%  Generate the list of characters needed to print a string.

-spec write_string(String) -> chars() when
    String :: string().

write_string(S) ->
    %"
    write_string(S, $").
%% write_char(Char) -> [char()].
%%  Generate the list of characters needed to print a character constant.
%%  Must special case SPACE, $\s, here.

-spec write_char(Char) -> chars() when
    Char :: char().

%Must special case this.
write_char($\s) ->
    "$\\s";
write_char(C) when is_integer(C), C >= $\000 ->
    [$$ | string_char(unicode_as_unicode, C, -1, [])].

%% @private
format_p_string([], Acc) ->
    [$", lists:reverse(Acc), $"];
format_p_string([8 | T], Acc) ->
    format_p_string(T, ["\\b" | Acc]);
format_p_string([9 | T], Acc) ->
    format_p_string(T, ["\\t" | Acc]);
format_p_string([10 | T], Acc) ->
    format_p_string(T, ["\\n" | Acc]);
format_p_string([11 | T], Acc) ->
    format_p_string(T, ["\\v" | Acc]);
format_p_string([12 | T], Acc) ->
    format_p_string(T, ["\\f" | Acc]);
format_p_string([13 | T], Acc) ->
    format_p_string(T, ["\\r" | Acc]);
format_p_string([27 | T], Acc) ->
    format_p_string(T, ["\\e" | Acc]);
format_p_string([H | T], Acc) ->
    format_p_string(T, [H | Acc]).

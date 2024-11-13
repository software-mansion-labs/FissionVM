%%
%% %CopyrightBegin%
%%
%% Copyright Ericsson AB 1997-2022. All Rights Reserved.
%%
%% Licensed under the Apache License, Version 2.0 (the "License");
%% you may not use this file except in compliance with the License.
%% You may obtain a copy of the License at
%%
%%     http://www.apache.org/licenses/LICENSE-2.0
%%
%% Unless required by applicable law or agreed to in writing, software
%% distributed under the License is distributed on an "AS IS" BASIS,
%% WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
%% See the License for the specific language governing permissions and
%% limitations under the License.
%%
%% %CopyrightEnd%
%%
-module(filename).

-removed([{find_src, '_', "use filelib:find_source/1,3 instead"}]).

-removed([{safe_relative_path, 1, "use filelib:safe_relative_path/2 instead"}]).

%% Purpose: Provides generic manipulation of filenames.
%%
%% Generally, these functions accept filenames in the native format
%% for the current operating system (Unix or Windows).
%% Deep characters lists (as returned by io_lib:format()) are accepted;
%% resulting strings will always be flat.
%%
%% Implementation note: We used to only flatten if the list turned out
%% to be deep. Now that atoms are allowed in deep lists, in most cases
%% we flatten the arguments immediately on function entry as that makes
%% it easier to ensure that the code works.

%%
%% *** Requirements on Raw Filename Format ***
%%
%% These requirements are due to the 'filename' module
%% in stdlib. This since it is documented that it
%% should be able to operate on raw filenames as well
%% as ordinary filenames.
%%
%% A raw filename *must* be a byte sequence where:
%% 1. Codepoints 0-127 (7-bit ascii) *must* be encoded
%%    as a byte with the corresponding value. That is,
%%    the most significant bit in the byte encoding the
%%    codepoint is never set.
%% 2. Codepoints greater than 127 *must* be encoded
%%    with the most significant bit set in *every* byte
%%    encoding it.
%%
%% Latin1 and UTF-8 meet these requirements while
%% UTF-16 and UTF-32 don't.
%%
%% On Windows filenames are natively stored as malformed
%% UTF-16LE (lonely surrogates may appear). A more correct
%% description than UTF-16 would be an array of 16-bit
%% words... In order to meet the requirements of the
%% raw file format we convert the malformed UTF-16LE to
%% malformed UTF-8 which meet the requirements.
%%
%% Note that these requirements are today only OTP
%% internal (erts-stdlib internal) requirements that
%% could be changed.
%%

-export([basename/1, basename/2, dirname/1]).

%% Undocumented and unsupported exports.
-export([append/2]).

-define(IS_DRIVELETTER(Letter),
    is_integer(Letter) andalso
        ($A =< Letter andalso Letter =< $Z orelse
            $a =< Letter andalso Letter =< $z)
).

%% Converts a relative filename to an absolute filename
%% or the filename itself if it already is an absolute filename
%% Note that no attempt is made to create the most beatiful
%% absolute name since this can give incorrect results on
%% file systems which allows links.
%% Examples:
%% Assume (for UNIX) current directory "/usr/local"
%% Assume (for WIN32) current directory "D:/usr/local"
%%
%% (for Unix) : absname("foo") -> "/usr/local/foo"
%% (for WIN32): absname("foo") -> "D:/usr/local/foo"
%% (for Unix) : absname("../x") -> "/usr/local/../x"
%% (for WIN32): absname("../x") -> "D:/usr/local/../x"
%% (for Unix) : absname("/") -> "/"
%% (for WIN32): absname("/") -> "D:/"

%% Returns the part of the filename after the last directory separator,
%% or the filename itself if it has no separators.
%%
%% Examples: basename("foo") -> "foo"
%%           basename("/usr/foo") -> "foo"
%%           basename("/usr/foo/") -> "foo"  (trailing slashes ignored)
%%           basename("/") -> []

-spec basename(Filename) -> file:filename_all() when
    Filename ::
        file:name_all().

basename(Name) when is_binary(Name) ->
    case os:type() of
        {win32, _} -> win_basenameb(Name);
        _ -> basenameb(Name, [<<"/">>])
    end;
basename(Name0) ->
    Name1 = flatten(Name0),
    {DirSep2, DrvSep} = separators(),
    Name = skip_prefix(Name1, DrvSep),
    basename1(Name, Name, DirSep2).

win_basenameb(<<Letter, $:, Rest/binary>>) when
    ?IS_DRIVELETTER(Letter)
->
    basenameb(Rest, [<<"/">>, <<"\\">>]);
win_basenameb(O) ->
    basenameb(O, [<<"/">>, <<"\\">>]).

basenameb(Bin, Sep) ->
    Parts = [
        X
     || X <- binary:split(Bin, Sep, [global]), X =/= <<>>
    ],
    if
        Parts =:= [] -> <<>>;
        true -> lists:last(Parts)
    end.

basename1([$/], Tail0, _DirSep2) ->
    %% End of filename -- must get rid of trailing directory separator.
    [_ | Tail] = lists:reverse(Tail0),
    lists:reverse(Tail);
basename1([$/ | Rest], _Tail, DirSep2) ->
    basename1(Rest, Rest, DirSep2);
basename1([DirSep2 | Rest], Tail, DirSep2) when
    is_integer(DirSep2)
->
    basename1([$/ | Rest], Tail, DirSep2);
basename1([Char | Rest], Tail, DirSep2) when
    is_integer(Char)
->
    basename1(Rest, Tail, DirSep2);
basename1([], Tail, _DirSep2) ->
    Tail.

skip_prefix(Name, false) ->
    Name;
skip_prefix([L, DrvSep | Name], DrvSep) when
    ?IS_DRIVELETTER(L)
->
    Name;
skip_prefix(Name, _) ->
    Name.

%% Returns the last component of the filename, with the given
%% extension stripped.  Use this function if you want
%% to remove an extension that might or might not be there.
%% Use rootname(basename(File)) if you want to remove an extension
%% that you know exists, but you are not sure which one it is.
%%
%% Example: basename("~/src/kalle.erl", ".erl") -> "kalle"
%%	    basename("~/src/kalle.jam", ".erl") -> "kalle.jam"
%%	    basename("~/src/kalle.old.erl", ".erl") -> "kalle.old"
%%
%%	    rootname(basename("xxx.jam")) -> "xxx"
%%	    rootname(basename("xxx.erl")) -> "xxx"

-spec basename(
    Filename,
    Ext
) -> file:filename_all() when
    Filename ::
        file:name_all(),
    Ext :: file:name_all().

basename(Name, Ext) when
    is_binary(Name), is_list(Ext)
->
    basename(Name, filename_string_to_binary(Ext));
basename(Name, Ext) when
    is_list(Name), is_binary(Ext)
->
    basename(filename_string_to_binary(Name), Ext);
basename(Name, Ext) when
    is_binary(Name), is_binary(Ext)
->
    BName = basename(Name),
    LAll = byte_size(Name),
    LN = byte_size(BName),
    LE = byte_size(Ext),
    case LN - LE of
        Neg when Neg < 0 -> BName;
        Pos ->
            StartLen = LAll - Pos - LE,
            case Name of
                <<_:StartLen/binary, Part:Pos/binary, Ext/binary>> ->
                    Part;
                _Other ->
                    BName
            end
    end;
basename(Name0, Ext0) ->
    Name = flatten(Name0),
    Ext = flatten(Ext0),
    {DirSep2, DrvSep} = separators(),
    NoPrefix = skip_prefix(Name, DrvSep),
    basename(NoPrefix, Ext, [], DirSep2).

basename(Ext, Ext, Tail, _DrvSep2) ->
    lists:reverse(Tail);
basename([$/], Ext, Tail, DrvSep2) ->
    basename([], Ext, Tail, DrvSep2);
basename([$/ | Rest], Ext, _Tail, DrvSep2) ->
    basename(Rest, Ext, [], DrvSep2);
basename([DirSep2 | Rest], Ext, Tail, DirSep2) when
    is_integer(DirSep2)
->
    basename([$/ | Rest], Ext, Tail, DirSep2);
basename([Char | Rest], Ext, Tail, DrvSep2) when
    is_integer(Char)
->
    basename(Rest, Ext, [Char | Tail], DrvSep2);
basename([], _Ext, Tail, _DrvSep2) ->
    lists:reverse(Tail).

%% Returns the directory part of a pathname.
%%
%% Example: dirname("/usr/src/kalle.erl") -> "/usr/src",
%%	    dirname("kalle.erl") -> "."

-spec dirname(Filename) -> file:filename_all() when
    Filename ::
        file:name_all().

dirname(Name) when is_binary(Name) ->
    {Dsep, Drivesep} = separators(),
    SList =
        case Dsep of
            Sep when is_integer(Sep) -> [<<Sep>>];
            _ -> []
        end,
    {XPart0, Dirs} =
        case Drivesep of
            X when is_integer(X) ->
                case Name of
                    <<DL, X, Rest/binary>> when
                        ?IS_DRIVELETTER(DL)
                    ->
                        {<<DL, X>>, Rest};
                    _ ->
                        {<<>>, Name}
                end;
            _ ->
                {<<>>, Name}
        end,
    Parts0 = binary:split(
        Dirs,
        [<<"/">> | SList],
        [global]
    ),
    %% Fairly short lists of parts, OK to reverse twice...
    Parts =
        case Parts0 of
            [] -> [];
            _ -> lists:reverse(fstrip(tl(lists:reverse(Parts0))))
        end,
    XPart =
        case {Parts, XPart0} of
            {[], <<>>} -> <<".">>;
            _ -> XPart0
        end,
    dirjoin(Parts, XPart, <<"/">>);
dirname(Name0) ->
    Name = flatten(Name0),
    dirname(Name, [], [], separators()).

dirname([$/ | Rest], Dir, File, Seps) ->
    dirname(Rest, File ++ Dir, [$/], Seps);
dirname([DirSep | Rest], Dir, File, {DirSep, _} = Seps) when
    is_integer(DirSep)
->
    dirname(Rest, File ++ Dir, [$/], Seps);
dirname([Dl, DrvSep | Rest], [], [], {_, DrvSep} = Seps) when
    is_integer(DrvSep), ?IS_DRIVELETTER(Dl)
->
    dirname(Rest, [DrvSep, Dl], [], Seps);
dirname([Char | Rest], Dir, File, Seps) when
    is_integer(Char)
->
    dirname(Rest, Dir, [Char | File], Seps);
dirname([], [], File, _Seps) ->
    case lists:reverse(File) of
        [$/ | _] -> [$/];
        _ -> "."
    end;
dirname([], [$/ | Rest], File, Seps) ->
    dirname([], Rest, File, Seps);
dirname([], [DrvSep, Dl], File, {_, DrvSep}) ->
    case lists:reverse(File) of
        [$/ | _] -> [Dl, DrvSep, $/];
        _ -> [Dl, DrvSep]
    end;
dirname([], Dir, _, _) ->
    lists:reverse(Dir).

%% Compatibility with lists variant, remove trailing slashes
fstrip([<<>>, X | Y]) -> fstrip([X | Y]);
fstrip(A) -> A.

dirjoin([<<>> | T], Acc, Sep) ->
    dirjoin1(T, <<Acc/binary, "/">>, Sep);
dirjoin(A, B, C) ->
    dirjoin1(A, B, C).

dirjoin1([], Acc, _) -> Acc;
dirjoin1([One], Acc, _) -> <<Acc/binary, One/binary>>;
dirjoin1([H | T], Acc, Sep) -> dirjoin(T, <<Acc/binary, H/binary, Sep/binary>>, Sep).

%% Internal function to join an absolute name and a relative name.
%% It is the responsibility of the caller to ensure that RelativeName
%% is relative.

%% Appends a directory separator and a pathname component to
%% a given base directory, which is is assumed to be normalised
%% by a previous call to join/{1,2}.

-spec append(
    file:filename_all(),
    file:name_all()
) -> file:filename_all().

append(Dir, Name) when
    is_binary(Dir), is_binary(Name)
->
    <<Dir/binary, $/:8, Name/binary>>;
append(Dir, Name) when is_binary(Dir) ->
    append(Dir, filename_string_to_binary(Name));
append(Dir, Name) when is_binary(Name) ->
    append(filename_string_to_binary(Dir), Name);
append(Dir, Name) ->
    Dir ++ [$/ | Name].

%% Returns one of absolute, relative or volumerelative.
%%
%% absolute	The pathname refers to a specific file on a specific
%%		volume.  Example: /usr/local/bin/ (on Unix),
%%		h:/port_test (on Windows).
%% relative	The pathname is relative to the current working directory
%%		on the current volume.  Example:  foo/bar, ../src
%% volumerelative  The pathname is relative to the current working directory
%%		on the specified volume, or is a specific file on the
%%		current working volume.  (Windows only)
%%		Example: a:bar.erl, /temp/foo.erl

%% flatten(List)
%%  Flatten a list, also accepting atoms.

-spec flatten(Filename) -> file:filename_all() when
    Filename ::
        file:name_all().

flatten(Bin) when is_binary(Bin) -> Bin;
flatten(List) -> do_flatten(List, []).

do_flatten([H | T], Tail) when is_list(H) ->
    do_flatten(H, do_flatten(T, Tail));
do_flatten([H | T], Tail) when is_atom(H) ->
    atom_to_list(H) ++ do_flatten(T, Tail);
do_flatten([H | T], Tail) ->
    [H | do_flatten(T, Tail)];
do_flatten([], Tail) ->
    Tail;
do_flatten(Atom, Tail) when is_atom(Atom) ->
    atom_to_list(Atom) ++ flatten(Tail).

filename_string_to_binary(List) ->
    case
        unicode:characters_to_binary(
            flatten(List),
            unicode,
            file:native_name_encoding()
        )
    of
        {error, _, _} -> erlang:error(badarg);
        Bin when is_binary(Bin) -> Bin
    end.

separators() -> {false, false}.

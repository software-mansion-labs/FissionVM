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

-export([basename/1, basename/2, dirname/1, absname/1, join/1, join/2,split/1, pathtype/1]).

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

-spec absname(Filename) -> file:filename_all() when
    Filename :: file:name_all().
absname(Name) ->
    {ok, Cwd} = file:get_cwd(),
    absname(Name, Cwd).

-spec absname(Filename, Dir) -> file:filename_all() when
    Filename :: file:name_all(),
    Dir :: file:name_all().
absname(Name, AbsBase) when is_binary(Name), is_list(AbsBase) ->
    absname(Name, filename_string_to_binary(AbsBase));
absname(Name, AbsBase) when is_list(Name), is_binary(AbsBase) ->
    absname(filename_string_to_binary(Name), AbsBase);
absname(Name, AbsBase) ->
    case pathtype(Name) of
        relative ->
            absname_join(AbsBase, Name);
        absolute ->
            %% We must flatten the filename before passing it into join/1,
            %% or we will get slashes inserted into the wrong places.
            join([flatten(Name)]);
        volumerelative ->
            absname_vr(split(Name), split(AbsBase), AbsBase)
    end.

%% Handles volumerelative names (on Windows only).

absname_vr([<<"/">> | Rest1], [Volume | _], _AbsBase) ->
    %% Absolute path on current drive.
    join([Volume | Rest1]);
absname_vr([<<X, $:>> | Rest1], [<<X, _/binary>> | _], AbsBase) ->
    %% Relative to current directory on current drive.
    absname(join(Rest1), AbsBase);
absname_vr([<<X, $:>> | Name], _, _AbsBase) ->
    %% Relative to current directory on another drive.
    Dcwd =
        case file:get_cwd([X, $:]) of
            {ok, Dir} -> filename_string_to_binary(Dir);
            {error, _} -> <<X, $:, $/>>
        end,
    absname(join(Name), Dcwd);
absname_vr(["/" | Rest1], [Volume | _], _AbsBase) ->
    %% Absolute path on current drive.
    join([Volume | Rest1]);
absname_vr([[X, $:] | Rest1], [[X | _] | _], AbsBase) ->
    %% Relative to current directory on current drive.
    absname(join(Rest1), AbsBase);
absname_vr([[X, $:] | Name], _, _AbsBase) ->
    %% Relative to current directory on another drive.
    Dcwd =
        case file:get_cwd([X, $:]) of
            {ok, Dir} -> Dir;
            {error, _} -> [X, $:, $/]
        end,
    absname(join(Name), Dcwd).

%% Joins a relative filename to an absolute base.
%% This is just a join/2, but assumes that
%% AbsBase must be absolute and Name must be relative.

-spec absname_join(Dir, Filename) -> file:filename_all() when
    Dir :: file:name_all(),
    Filename :: file:name_all().
absname_join(AbsBase, Name) ->
    join(AbsBase, flatten(Name)).

-spec join(Components) -> file:filename_all() when
    Components :: [file:name_all()].
join([Name1, Name2 | Rest]) ->
    join([join(Name1, Name2) | Rest]);
join([Name]) when is_list(Name) ->
    join1(Name, [], [], major_os_type());
join([Name]) when is_binary(Name) ->
    join1b(Name, <<>>, [], major_os_type());
join([Name]) when is_atom(Name) ->
    join([atom_to_list(Name)]).

%% Joins two filenames with directory separators.

-spec join(Name1, Name2) -> file:filename_all() when
    Name1 :: file:name_all(),
    Name2 :: file:name_all().
join(Name1, Name2) when is_list(Name1), is_list(Name2) ->
    OsType = major_os_type(),
    case pathtype(Name2) of
        relative -> join1(Name1, Name2, [], OsType);
        _Other -> join1(Name2, [], [], OsType)
    end;
join(Name1, Name2) when is_binary(Name1), is_list(Name2) ->
    join(Name1, filename_string_to_binary(Name2));
join(Name1, Name2) when is_list(Name1), is_binary(Name2) ->
    join(filename_string_to_binary(Name1), Name2);
join(Name1, Name2) when is_binary(Name1), is_binary(Name2) ->
    OsType = major_os_type(),
    case pathtype(Name2) of
        relative -> join1b(Name1, Name2, [], OsType);
        _Other -> join1b(Name2, <<>>, [], OsType)
    end;
join(Name1, Name2) when is_atom(Name1) ->
    join(atom_to_list(Name1), Name2);
join(Name1, Name2) when is_atom(Name2) ->
    join(Name1, atom_to_list(Name2)).

%% Internal function to join an absolute name and a relative name.
%% It is the responsibility of the caller to ensure that RelativeName
%% is relative.

join1([UcLetter, $: | Rest], RelativeName, [], win32) when
    is_integer(UcLetter), UcLetter >= $A, UcLetter =< $Z
->
    join1(Rest, RelativeName, [$:, UcLetter + $a - $A], win32);
join1([$\\, $\\ | Rest], RelativeName, [], win32) ->
    join1([$/, $/ | Rest], RelativeName, [], win32);
join1([$/, $/ | Rest], RelativeName, [], win32) ->
    join1(Rest, RelativeName, [$/, $/], win32);
join1([$\\ | Rest], RelativeName, Result, win32) ->
    join1([$/ | Rest], RelativeName, Result, win32);
join1([$/ | Rest], RelativeName, [$., $/ | Result], OsType) ->
    join1(Rest, RelativeName, [$/ | Result], OsType);
join1([$/ | Rest], RelativeName, [$/ | Result], OsType) ->
    join1(Rest, RelativeName, [$/ | Result], OsType);
join1([], [], Result, OsType) ->
    maybe_remove_dirsep(Result, OsType);
join1([], RelativeName, [$: | Rest], win32) ->
    join1(RelativeName, [], [$: | Rest], win32);
join1([], RelativeName, [$/ | Result], OsType) ->
    join1(RelativeName, [], [$/ | Result], OsType);
join1([], RelativeName, [$., $/ | Result], OsType) ->
    join1(RelativeName, [], [$/ | Result], OsType);
join1([], RelativeName, Result, OsType) ->
    join1(RelativeName, [], [$/ | Result], OsType);
join1([[_ | _] = List | Rest], RelativeName, Result, OsType) ->
    join1(List ++ Rest, RelativeName, Result, OsType);
join1([[] | Rest], RelativeName, Result, OsType) ->
    join1(Rest, RelativeName, Result, OsType);
join1([Char | Rest], RelativeName, Result, OsType) when is_integer(Char) ->
    join1(Rest, RelativeName, [Char | Result], OsType);
join1([Atom | Rest], RelativeName, Result, OsType) when is_atom(Atom) ->
    join1(atom_to_list(Atom) ++ Rest, RelativeName, Result, OsType).

join1b(<<UcLetter, $:, Rest/binary>>, RelativeName, [], win32) when
    is_integer(UcLetter), UcLetter >= $A, UcLetter =< $Z
->
    join1b(Rest, RelativeName, [$:, UcLetter + $a - $A], win32);
join1b(<<$\\, $\\, Rest/binary>>, RelativeName, [], win32) ->
    join1b(<<$/, $/, Rest/binary>>, RelativeName, [], win32);
join1b(<<$/, $/, Rest/binary>>, RelativeName, [], win32) ->
    join1b(Rest, RelativeName, [$/, $/], win32);
join1b(<<$\\, Rest/binary>>, RelativeName, Result, win32) ->
    join1b(<<$/, Rest/binary>>, RelativeName, Result, win32);
join1b(<<$/, Rest/binary>>, RelativeName, [$., $/ | Result], OsType) ->
    join1b(Rest, RelativeName, [$/ | Result], OsType);
join1b(<<$/, Rest/binary>>, RelativeName, [$/ | Result], OsType) ->
    join1b(Rest, RelativeName, [$/ | Result], OsType);
join1b(<<>>, <<>>, Result, OsType) ->
    list_to_binary(maybe_remove_dirsep(Result, OsType));
join1b(<<>>, RelativeName, [$: | Rest], win32) ->
    join1b(RelativeName, <<>>, [$: | Rest], win32);
join1b(<<>>, RelativeName, [$/, $/ | Result], win32) ->
    join1b(RelativeName, <<>>, [$/, $/ | Result], win32);
join1b(<<>>, RelativeName, [$/ | Result], OsType) ->
    join1b(RelativeName, <<>>, [$/ | Result], OsType);
join1b(<<>>, RelativeName, [$., $/ | Result], OsType) ->
    join1b(RelativeName, <<>>, [$/ | Result], OsType);
join1b(<<>>, RelativeName, Result, OsType) ->
    join1b(RelativeName, <<>>, [$/ | Result], OsType);
join1b(<<Char, Rest/binary>>, RelativeName, Result, OsType) when is_integer(Char) ->
    join1b(Rest, RelativeName, [Char | Result], OsType).

maybe_remove_dirsep([$/, $:, Letter], win32) ->
    [Letter, $:, $/];
maybe_remove_dirsep([$/], _) ->
    [$/];
maybe_remove_dirsep([$/, $/], win32) ->
    [$/, $/];
maybe_remove_dirsep([$/ | Name], _) ->
    lists:reverse(Name);
maybe_remove_dirsep(Name, _) ->
    lists:reverse(Name).

major_os_type() ->
    {OsT, _} = os:type(),
    OsT.


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

-spec pathtype(Path) -> 'absolute' | 'relative' | 'volumerelative' when
      Path :: file:name_all().
pathtype(Atom) when is_atom(Atom) ->
    pathtype(atom_to_list(Atom));
pathtype(Name) when is_list(Name) or is_binary(Name) ->
    case os:type() of
	{win32, _} ->
	    win32_pathtype(Name);
	{_, _}  ->
	    unix_pathtype(Name)
    end.

unix_pathtype(<<$/,_/binary>>) ->
    absolute;
unix_pathtype([$/|_]) ->
    absolute;
unix_pathtype([List|Rest]) when is_list(List) ->
    unix_pathtype(List++Rest);
unix_pathtype([Atom|Rest]) when is_atom(Atom) ->
    unix_pathtype(atom_to_list(Atom)++Rest);
unix_pathtype(_) ->
    relative.

win32_pathtype([List|Rest]) when is_list(List) ->
    win32_pathtype(List++Rest);
win32_pathtype([Atom|Rest]) when is_atom(Atom) ->
    win32_pathtype(atom_to_list(Atom)++Rest);
win32_pathtype([Char, List|Rest]) when is_list(List) ->
    win32_pathtype([Char|List++Rest]);
win32_pathtype(<<$/, $/, _/binary>>) -> absolute;
win32_pathtype(<<$\\, $/, _/binary>>) -> absolute;
win32_pathtype(<<$/, $\\, _/binary>>) -> absolute;
win32_pathtype(<<$\\, $\\, _/binary>>) -> absolute;
win32_pathtype(<<$/, _/binary>>) -> volumerelative;
win32_pathtype(<<$\\, _/binary>>) -> volumerelative;
win32_pathtype(<<_Letter, $:, $/, _/binary>>) -> absolute;
win32_pathtype(<<_Letter, $:, $\\, _/binary>>) -> absolute;
win32_pathtype(<<_Letter, $:, _/binary>>) -> volumerelative;
win32_pathtype([$/, $/|_]) -> absolute;
win32_pathtype([$\\, $/|_]) -> absolute;
win32_pathtype([$/, $\\|_]) -> absolute;
win32_pathtype([$\\, $\\|_]) -> absolute;
win32_pathtype([$/|_]) -> volumerelative;
win32_pathtype([$\\|_]) -> volumerelative;
win32_pathtype([C1, C2, List|Rest]) when is_list(List) ->
    pathtype([C1, C2|List++Rest]);
win32_pathtype([_Letter, $:, $/|_]) -> absolute;
win32_pathtype([_Letter, $:, $\\|_]) -> absolute;
win32_pathtype([_Letter, $:|_]) -> volumerelative;
win32_pathtype(_) 		  -> relative.

%% Returns a list whose elements are the path components in the filename.
%%
%% Examples:	
%% split("/usr/local/bin") -> ["/", "usr", "local", "bin"]
%% split("foo/bar") -> ["foo", "bar"]
%% split("a:\\msdev\\include") -> ["a:/", "msdev", "include"]

-spec split(Filename) -> Components when
      Filename :: file:name_all(),
      Components :: [file:name_all()].
split(Name) when is_binary(Name) ->
    case os:type() of
	{win32, _} -> win32_splitb(Name);
	_  -> unix_splitb(Name)
    end;

split(Name0) ->
    Name = flatten(Name0),
    case os:type() of
	{win32, _} -> win32_split(Name);
	_  -> unix_split(Name)
    end.


unix_splitb(Name) ->
    L = binary:split(Name,[<<"/">>],[global]),
    LL = case L of
	     [<<>>|Rest] when Rest =/= [] ->
		 [<<"/">>|Rest];
	     _ ->
		 L
	 end,
    [ X || X <- LL, X =/= <<>>].


fix_driveletter(Letter0) ->
    if
	Letter0 >= $A, Letter0 =< $Z ->  
	    Letter0+$a-$A;
	true ->
	    Letter0
    end.
win32_splitb(<<Letter0,$:, Slash, Rest/binary>>) when (((Slash =:= $\\) orelse (Slash =:= $/)) andalso
							 ?IS_DRIVELETTER(Letter0)) ->
    Letter = fix_driveletter(Letter0),
    L = binary:split(Rest,[<<"/">>,<<"\\">>],[global]),
    [<<Letter,$:,$/>> | [ X || X <- L, X =/= <<>> ]]; 
win32_splitb(<<Letter0,$:,Rest/binary>>) when ?IS_DRIVELETTER(Letter0) ->
    Letter = fix_driveletter(Letter0),
    L = binary:split(Rest,[<<"/">>,<<"\\">>],[global]),
    [<<Letter,$:>> | [ X || X <- L, X =/= <<>> ]];
win32_splitb(<<Slash,Slash,Rest/binary>>) when ((Slash =:= $\\) orelse (Slash =:= $/)) ->
    L = binary:split(Rest,[<<"/">>,<<"\\">>],[global]),
    [<<"//">> | [ X || X <- L, X =/= <<>> ]];
win32_splitb(<<Slash,Rest/binary>>) when ((Slash =:= $\\) orelse (Slash =:= $/)) ->
    L = binary:split(Rest,[<<"/">>,<<"\\">>],[global]),
    [<<$/>> | [ X || X <- L, X =/= <<>> ]];
win32_splitb(Name) ->
    L = binary:split(Name,[<<"/">>,<<"\\">>],[global]),
    [ X || X <- L, X =/= <<>> ].
    

unix_split(Name) ->
    split(Name, [], unix).

win32_split([Slash,Slash|Rest]) when ((Slash =:= $\\) orelse (Slash =:= $/)) ->
    split(Rest, [[$/,$/]], win32);
win32_split([$\\|Rest]) ->
    win32_split([$/|Rest]);
win32_split([X, $\\|Rest]) when is_integer(X) ->
    win32_split([X, $/|Rest]);
win32_split([X, Y, $\\|Rest]) when is_integer(X), is_integer(Y) ->
    win32_split([X, Y, $/|Rest]);
win32_split([UcLetter, $:|Rest])
  when is_integer(UcLetter), $A =< UcLetter, UcLetter =< $Z ->
    win32_split([UcLetter+$a-$A, $:|Rest]);
win32_split([Letter, $:, $/|Rest]) ->
    split(Rest, [], [[Letter, $:, $/]], win32);
win32_split([Letter, $:|Rest]) ->
    split(Rest, [], [[Letter, $:]], win32);
win32_split(Name) ->
    split(Name, [], win32).

split([$/|Rest], Components, OsType) ->
    split(Rest, [], [[$/]|Components], OsType);
split([$\\|Rest], Components, win32) ->
    split(Rest, [], [[$/]|Components], win32);
split(RelativeName, Components, OsType) ->
    split(RelativeName, [], Components, OsType).

split([$\\|Rest], Comp, Components, win32) ->
    split([$/|Rest], Comp, Components, win32);
split([$/|Rest], [], Components, OsType) ->
    split(Rest, [], Components, OsType);
split([$/|Rest], Comp, Components, OsType) ->
    split(Rest, [], [lists:reverse(Comp)|Components], OsType);
split([Char|Rest], Comp, Components, OsType) when is_integer(Char) ->
    split(Rest, [Char|Comp], Components, OsType);
split([], [], Components, _OsType) ->
    lists:reverse(Components);
split([], Comp, Components, OsType) ->
    split([], [], [lists:reverse(Comp)|Components], OsType).
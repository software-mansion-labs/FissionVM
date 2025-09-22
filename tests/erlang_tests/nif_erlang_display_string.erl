-module(nif_erlang_display_string).

-export([start/0, id/1]).

-define(ID(Arg), ?MODULE:id(Arg)).

start() ->
  ok = test_charlist(),
  ok = test_binary(),
  ok = test_badarg(),
  0.

test_charlist() ->
  true = erlang:display_string(stdout, ?ID("hello\n")),
  true = erlang:display_string(stdout, ?ID([])),

  true = erlang:display_string(stderr, ?ID("hello\n")),
  true = erlang:display_string(stderr, ?ID([])),
  ok.

test_binary() ->
  true = erlang:display_string(stdout, ?ID(<<"hello\n">>)),
  true = erlang:display_string(stdout, ?ID(<<>>)),

  true = erlang:display_string(stderr, ?ID(<<"hello\n">>)),
  true = erlang:display_string(stderr, ?ID(<<>>)),
  ok.

test_badarg() ->
  true = assert_badarg(no_device, ?ID("hello\n")),
  true = assert_badarg(no_device, ?ID(<<"hello\n">>)),
  true = assert_badarg(stdout, ?ID(true)),
  true = assert_badarg(stdout, ?ID([3.1, 0])),
  true = assert_badarg(stdout, ?ID({ok, <<"hello\n">>})),
  ok.

id(T) ->
  T.

assert_badarg(Device, Chars) ->
  try erlang:display_string(Device, Chars) of
    _Res ->
      false
  catch
    error:badarg ->
      true;
    _:_ ->
      false
  end.

-module(nif_erlang_display_string).

-export([start/0, id/1]).

-define(ID(Arg), ?MODULE:id(Arg)).

start() ->
  ok = test_charlist(),
  ok = test_binary(),
  0.

test_charlist() ->
  true = erlang:display_string(stdout, ?ID("hello\n")),
  true = erlang:display_string(stderr, ?ID("hello\n")),
  ok.

test_binary() ->
  true = erlang:display_string(stdout, ?ID(<<"hello\n">>)),
  true = erlang:display_string(stderr, ?ID(<<"hello\n">>)),
  ok.

id(T) ->
  T.

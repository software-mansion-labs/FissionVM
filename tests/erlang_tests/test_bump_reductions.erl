-module(test_bump_reductions).

-export([start/0]).

start() ->
    erlang:bump_reductions(100),
    0.

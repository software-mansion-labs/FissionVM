-module(test_lists_member).

-export([start/0]).

start() ->
    true = lists:member(2, [1, 2, 3]),
    0.

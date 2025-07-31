-module(test_lists_keyfind).

-export([start/0]).

start() ->
    {b, 2} = lists:keyfind(b, 1, [{a, 1}, {b, 2}, {c, 3}]),
    false = lists:keyfind(b, 2, [{a, 1}, {b, 2}, {c, 3}]),
    0.

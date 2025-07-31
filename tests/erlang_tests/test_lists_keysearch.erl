-module(test_lists_keysearch).

-export([start/0]).

start() ->
    {value, {b, 2}} = lists:keysearch(b, 1, [{a, 1}, {b, 2}, {c, 3}]),
    false = lists:keysearch(b, 2, [{a, 1}, {b, 2}, {c, 3}]),
    0.

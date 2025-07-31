-module(test_lists_keymember).

-export([start/0]).

start() ->
    true = lists:keymember(b, 0, [{a, 1}, {b, 2}, {c, 3}]),
    false = lists:keymember(b, 2, [{a, 1}, {b, 2}, {c, 3}]),
    0.

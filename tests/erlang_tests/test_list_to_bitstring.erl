-module(test_list_to_bitstring).

-export([start/0]).

start() ->
    <<"hello">> = erlang:list_to_bitstring("hello"),
    0.

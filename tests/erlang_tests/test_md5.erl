-module(test_md5).

-export([start/0]).

start() ->
    <<93, 65, 64, 42, 188, 75, 42, 118, 185, 113, 157, 145, 16, 23, 197, 146>> =
        erlang:md5("hello"),
    0.

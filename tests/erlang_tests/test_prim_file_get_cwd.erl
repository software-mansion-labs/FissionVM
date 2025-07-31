-module(test_prim_file_get_cwd).

-export([start/0]).

start() ->
    {ok, Cwd} = prim_file:get_cwd(),
    true = is_binary(Cwd),
    0.

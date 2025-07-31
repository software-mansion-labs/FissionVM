-module(test_avm_rand_splitmix64_next).

-export([start/0]).

start() ->
    {-5414281315512073941, -7046029254386353008} = avm_rand:splitmix64_next(123),
    0.

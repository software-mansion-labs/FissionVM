-module(avm_rand).

-export([splitmix64_next/1]).

splitmix64_next(_X) ->
  erlang:error(splitmix64_next_nif_missing).

-module(test_node).

-export([start/0]).

start() ->
    nonode@nohost = node(),
    nonode@nohost = node(self()),
    ok = guarded_fun(),
    ok = guarded_fun(self()),
    0.

guarded_fun() when node() == nonode@nohost -> ok.

guarded_fun(Pid) when node(Pid) == nonode@nohost -> ok.

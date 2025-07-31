-module(test_file_native_name_encoding).

-export([start/0]).

start() ->
    utf8 = file:native_name_encoding(),
    0.

-module(test_lists_keyfind).

-export([test/0, start/0]).

start() ->
    test().

test() ->
    {2, bob} = test_keyfind_with_existing_key(),
    false = test_keyfind_with_non_existing_key(),
    false = test_keyfind_with_empty_list(),
    false = test_keyfind_with_existing_key_on_different_position(),
    {here, it, is} = test_keyfind_with_existing_key_and_different_length_of_tuples(),
    ok.

test_keyfind_with_existing_key() ->
    Key = bob,
    Tuple1 = {1, alice},
    Tuple2 = {2, bob},
    Tuple3 = {3, carol},
    List = [Tuple1, Tuple2, Tuple3],
    lists:keyfind(Key, 2, List).

test_keyfind_with_existing_key_and_different_length_of_tuples() ->
    Key = is,
    Tuple1 = {1},
    Tuple2 = {2, bob},
    Tuple3 = {3, carol, singing, tree},
    Tuple4 = {here, it, is},
    List = [Tuple1, Tuple2, Tuple3, Tuple4],
    lists:keyfind(Key, 3, List).

test_keyfind_with_non_existing_key() ->
    Key = 4,
    Tuple1 = {1, alice},
    Tuple2 = {2, bob},
    Tuple3 = {3, carol},
    List = [Tuple1, Tuple2, Tuple3],
    lists:keyfind(Key, 1, List).

test_keyfind_with_empty_list() ->
    Key = 3,
    List = [],
    lists:keyfind(Key, 1, List).

test_keyfind_with_existing_key_on_different_position() ->
    Key = 1,
    Tuple1 = {1, alice},
    Tuple2 = {4, bob},
    Tuple3 = {3, carol},
    List = [Tuple1, Tuple2, Tuple3],
    lists:keyfind(Key, 2, List).

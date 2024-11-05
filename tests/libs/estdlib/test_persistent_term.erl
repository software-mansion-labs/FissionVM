-module(test_persistent_term).

-export([test/0]).

test() ->
    ok = test_put_without_initialized(),
    ok = test_get_with_empty_table(),
    ok = test_get_return_value(),
    ok = test_get_return_default().

test_put_without_initialized() ->
    persistent_term:put(key, "SECRET VALUE"),
    clean_after_test(),
    ok.

test_get_with_empty_table() ->
    Default = "SECRET VALUE",
    Default = persistent_term:get(key, Default),
    clean_after_test(),
    ok.

test_get_return_value() ->
    Value = "SECRET VALUE",
    persistent_term:put(key, Value),
    Value = persistent_term:get(key, "OTHER SECRET"),
    clean_after_test(),
    ok.

test_get_return_default() ->
    Value = "SECRET VALUE",
    persistent_term:put(key, "OTHER SECRET"),
    Value = persistent_term:get(other_key, Value),
    clean_after_test(),
    ok.

clean_after_test() ->
    ets:delete(persistent_term, key).

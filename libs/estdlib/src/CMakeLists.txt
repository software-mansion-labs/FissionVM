#
# This file is part of AtomVM.
#
# Copyright 2018-2021 Fred Dushin <fred@dushin.net>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0 OR LGPL-2.1-or-later
#

project(estdlib)

include(BuildErlang)

set(ERLANG_MODULES
    base64
    binary
    calendar
    cerl_clauses
    cerl_trees
    cerl
    code
    compile
    core_lib
    crypto
    persistent_term
    erl_anno
    erl_bifs
    erl_bits
    erl_error
    erl_eval
    erl_expand_records
    erl_features
    erl_internal
    erl_lint
    erl_parse.yrl
    elixir_config
    elixir_utils
    elixir_tokenizer
    elixir_interpolation
    elixir_erl_var
    elixir_erl_clauses
    elixir_sup
    elixir_aliases
    elixir_env
    elixir_clauses
    elixir_quote
    elixir_parser.yrl
    erl_scan
    erts_debug
    erts_internal
    ets
    eval_bits
    file
    filename
    gb_sets
    gen_event
    gen_server
    gen_statem
    gen_udp
    gen_udp_inet
    gen_udp_socket
    gen_tcp
    gen_tcp_inet
    gen_tcp_socket
    supervisor
    inet
    io_lib
    io
    lists
    maps
    math
    net
    orddict
    ordsets
    otp_internal
    logger
    logger_std_h
    proplists
    queue
    sets
    socket
    sofs
    ssl
    string
    sys_core_fold
    timer
    unicode
    unicode_util
    v3_core
    erlang
)

pack_archive(estdlib ${ERLANG_MODULES})

include(../../../version.cmake)

set(ESTDLIB_VERSION ${ATOMVM_BASE_VERSION})

install(
    DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/beams/
    DESTINATION lib/atomvm/lib/estdlib-${ESTDLIB_VERSION}/ebin
    FILES_MATCHING PATTERN "*.beam"
)

install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/estdlib.avm
    DESTINATION lib/atomvm/lib/estdlib-${ESTDLIB_VERSION}/ebin/
)

install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
    DESTINATION lib/atomvm/lib/estdlib-${ESTDLIB_VERSION}/src
    FILES_MATCHING PATTERN "*.erl"
)

%
% This file is part of AtomVM.
%
% Copyright 2025 Bartosz Błaszków <bartosz.blaszkow@swmansion.com>
%
% Licensed under the Apache License, Version 2.0 (the "License");
% you may not use this file except in compliance with the License.
% You may obtain a copy of the License at
%
%    http://www.apache.org/licenses/LICENSE-2.0
%
% Unless required by applicable law or agreed to in writing, software
% distributed under the License is distributed on an "AS IS" BASIS,
% WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
% See the License for the specific language governing permissions and
% limitations under the License.
%
% SPDX-License-Identifier: Apache-2.0 OR LGPL-2.1-or-later
%

-module(float2bin2short).

-export([start/0]).
-export([id/1]).

-define(ID(Arg), ?MODULE:id(Arg)).

% FIXME: When short is properly implemented, add cases when the representation
% becomes shorter using expotential notation, e.g.
% 1/1_000_000 => "1.0e-6"
start() ->
  F1 = ?ID(2.5) + ?ID(-1.0),
  Bin1 = erlang:float_to_binary(?ID(F1), [short]),
  F2 = ?ID(F1) + ?ID(0.5) * ?ID(-1.0),
  Bin2 = erlang:float_to_binary(?ID(F2), [short]),
  F3 = ?ID(F2) * ?ID(-1.0),
  Bin3 = id(erlang:float_to_binary(?ID(F3), [short])),
  F4 = ?ID(F2) + ?ID(F3),
  Bin4 = erlang:float_to_binary(?ID(F4), [short]),
  true = assert_bin_equal(Bin1, ?ID(<<"1.5">>)),
  true = assert_bin_equal(Bin2, ?ID(<<"1.0">>)),
  true = assert_bin_equal(Bin3, ?ID(<<"-1.0">>)),
  true = assert_bin_equal(Bin4, ?ID(<<"0.0">>)),
  true = assert_float_to_bin_badarg({1}, [short]),
  true = assert_float_to_bin_badarg(F4, [{short, 1}]),
  0.

assert_bin_equal(Bin1, Bin2) when byte_size(Bin1) == byte_size(Bin2) ->
  compare_bin(Bin1, Bin2, byte_size(Bin1) - 1);
assert_bin_equal(_Bin1, _Bin2) ->
  false.

compare_bin(_Bin1, _Bin2, -1) ->
  true;
compare_bin(Bin1, Bin2, Index) ->
  B1 = binary:at(Bin1, Index),
  case binary:at(Bin2, Index) of
    B1 ->
      compare_bin(Bin1, Bin2, Index - 1);
    _Any ->
      false
  end.

id(I) when is_float(I) ->
  I;
id(I) when is_binary(I) ->
  I.

assert_float_to_bin_badarg(F, O) ->
  try erlang:float_to_binary(F, O) of
    _Res ->
      false
  catch
    error:badarg ->
      true;
    _:_ ->
      false
  end.

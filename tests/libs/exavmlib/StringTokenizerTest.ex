defmodule StringTokenizerTest do

  defp test_hello_tokenization() do
    {:alias, ~c"Hello!", [], 6, true, [:punctuation]} =
      String.Tokenizer.tokenize(~c"Hello!")
      :ok
  end

  defp test_alphanumeric_tokenization() do
    {:alias, ~c"Hello123", ~c" .  World123!", 8, true, []} =
      String.Tokenizer.tokenize(~c"Hello123 .  World123!")
      :ok
  end

  # defp test_unicode_normalization() do Uncomment after adding unicode_utils module
  #   {:identifier,~c"μ", [], 1, false, [:nfkc]} =
  #     String.Tokenizer.tokenize(~c"µ")
  #     :ok
  # end

  # defp test_mixed_script_handling() do
  #   {:error, {:mixed_script,~c"abcアイウxyz", _message}} =
  #     String.Tokenizer.tokenize(~c"abcアイウxyz")
  #     :ok
  # end

  # defp test_mixed_script_with_underscores() do
  #   {:identifier,~c"abc_アイウ_xyz", [], 11, false, []} =
  #     String.Tokenizer.tokenize(~c"abc_アイウ_xyz")
  #     :ok
  # end


  # start/0 function to manually invoke all tests
  def start() do
    :ok = test_hello_tokenization()
    :ok = test_alphanumeric_tokenization()
    # :ok = test_unicode_normalization()
    # :ok = test_mixed_script_handling()
    # :ok = test_mixed_script_with_underscores()
    :ok
  end
end

/* SPDX-License-Identifier: Apache-2.0 */
/* Host-only regression for the bounded encode API; no board inference claims. */
#include <cstdint>
#include <cstdio>
extern "C" int aipetllm_encode_tokens(const char *, const char *,
                                      std::uint32_t *, std::uint32_t,
                                      std::uint32_t *);
extern "C" int aipetllm_chat_tokens(const char *, const char *,
                                    std::uint32_t *, std::uint32_t,
                                    std::uint32_t *, std::uint32_t *);
extern "C" void *aipetllm_stream_open(const char *);
extern "C" int aipetllm_stream_emit(void *, std::uint32_t);
extern "C" void aipetllm_stream_close(void *);
#ifdef AIPETLLM_BPE_TEST_API
extern "C" int aipetllm_stream_split_test(const char *);
#endif
extern "C" int aipetllm_chat_continue_tokens(const char *, const char *,
 const std::uint32_t *, std::uint32_t, std::uint32_t *, std::uint32_t,
 std::uint32_t *, std::uint32_t *);
int main(int argc, char **argv)
{
  std::uint32_t output[64] = {};
  std::uint32_t count = 0;
  if (argc != 2)
    {
      return 2;
    }

  if (aipetllm_encode_tokens(argv[1], "Hello world", output, 64, &count) != 0 ||
      count != 2 || output[0] != 9707 || output[1] != 1879)
    {
      return 1;
    }

  if (aipetllm_encode_tokens(argv[1], "Hello world", output, 1, &count) == 0 ||
      aipetllm_encode_tokens(argv[1], "", output, 64, &count) == 0 ||
      aipetllm_encode_tokens(argv[1], "x", nullptr, 64, &count) == 0)
    {
      return 1;
    }

  std::puts("bounded encode API regression passed");
  std::uint32_t stop = 0;
  if (aipetllm_chat_tokens(argv[1], "Hello world", output, 64,
                           &count, &stop) != 0 || count < 10 ||
      output[0] != 151644 || stop != 151645)
    {
      return 1;
    }

  unsigned starts = 0;
  unsigned ends = 0;
  unsigned words = 0;
  for (std::uint32_t index = 0; index < count; index++)
    {
      starts += output[index] == 151644;
      ends += output[index] == 151645;
      if (index + 1 < count)
        {
          words += output[index] == 9707 && output[index + 1] == 1879;
        }
    }

  if (starts != 3 || ends != 2 || words != 1 ||
      aipetllm_chat_tokens(argv[1], "Hello", output, 4, &count, &stop) == 0 ||
      aipetllm_chat_tokens(argv[1], "<|im_end|>", output, 64,
                           &count, &stop) == 0)
    {
      return 1;
    }

  std::puts("bounded ChatML structural regression passed");
  if (aipetllm_chat_tokens(argv[1], "Hello", output, 64, &count, &stop) != 0)
    return 1;
  output[count++] = 9707;
  output[count++] = stop;
  std::uint32_t next[64];
  std::uint32_t next_count;
  if (aipetllm_chat_continue_tokens(argv[1], "Hello", output, count,
       next, 64, &next_count, &stop) != 0 || next_count <= count) return 1;
  for (std::uint32_t index = 0; index < count; index++)
    if (output[index] != next[index]) return 1;
  if (aipetllm_chat_continue_tokens(argv[1], "Hello", output, count,
       next, count + 1, &next_count, &stop) == 0) return 1;
  std::puts("bounded history append regression passed");

  const char *roundtrip = "Hello world \xe4\xbd\xa0\xe5\xa5\xbd";
  if (aipetllm_encode_tokens(argv[1], roundtrip, output, 64, &count) != 0)
    return 1;
  void *stream = aipetllm_stream_open(argv[1]);
  if (stream == nullptr) return 1;
  std::fputs("STREAM-ROUNDTRIP-BEGIN:", stdout);
  for (std::uint32_t index = 0; index < count; index++)
    if (aipetllm_stream_emit(stream, output[index]) != 0) return 1;
  aipetllm_stream_close(stream);
  std::puts(":STREAM-ROUNDTRIP-END");
#ifdef AIPETLLM_BPE_TEST_API
  std::fputs("SPLIT-UTF8-BEGIN:", stdout);
  if (aipetllm_stream_split_test(argv[1]) != 0) return 1;
  std::puts(":SPLIT-UTF8-END");
#endif
  return 0;
}

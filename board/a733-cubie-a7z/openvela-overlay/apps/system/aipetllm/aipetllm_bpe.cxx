/****************************************************************************
 * apps/system/aipetllm/aipetllm_bpe.cxx
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <cerrno>
#include <cinttypes>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{

constexpr std::uint32_t GGUF_MAGIC = UINT32_C(0x46554747);
constexpr std::uint32_t GGUF_STRING = 8;
constexpr std::uint32_t GGUF_ARRAY = 9;
constexpr std::size_t KEY_LIMIT = 256;
constexpr std::size_t ENTRY_LIMIT = 1024 * 1024;

bool read_exact(FILE *stream, void *data, std::size_t size)
{
  return std::fread(data, 1, size, stream) == size;
}

bool skip_exact(FILE *stream, std::uint64_t size)
{
  while (size != 0)
    {
      long step = size > static_cast<std::uint64_t>(LONG_MAX) ?
                  LONG_MAX : static_cast<long>(size);
      if (std::fseek(stream, step, SEEK_CUR) != 0)
        {
          return false;
        }

      size -= static_cast<std::uint64_t>(step);
    }

  return true;
}

std::size_t scalar_size(std::uint32_t type)
{
  switch (type)
    {
      case 0:
      case 1:
      case 7:
        return 1;
      case 2:
      case 3:
        return 2;
      case 4:
      case 5:
      case 6:
        return 4;
      case 10:
      case 11:
      case 12:
        return 8;
      default:
        return 0;
    }
}

bool skip_value(FILE *stream, std::uint32_t type, unsigned int depth = 0)
{
  std::size_t size = scalar_size(type);
  std::uint32_t element;
  std::uint64_t count;
  std::uint64_t length;

  if (depth > 4)
    {
      return false;
    }

  if (size != 0)
    {
      return skip_exact(stream, size);
    }

  if (type == GGUF_STRING)
    {
      return read_exact(stream, &length, sizeof(length)) &&
             skip_exact(stream, length);
    }

  if (type != GGUF_ARRAY ||
      !read_exact(stream, &element, sizeof(element)) ||
      !read_exact(stream, &count, sizeof(count)))
    {
      return false;
    }

  size = scalar_size(element);
  if (size != 0)
    {
      return count <= UINT64_MAX / size &&
             skip_exact(stream, count * size);
    }

  for (std::uint64_t index = 0; index < count; index++)
    {
      if (!skip_value(stream, element, depth + 1))
        {
          return false;
        }
    }

  return true;
}

bool read_string(FILE *stream, std::string &value)
{
  std::uint64_t length;

  if (!read_exact(stream, &length, sizeof(length)) ||
      length > ENTRY_LIMIT)
    {
      return false;
    }

  value.resize(static_cast<std::size_t>(length));
  return length == 0 ||
         read_exact(stream, &value[0], static_cast<std::size_t>(length));
}

bool read_string_array(FILE *stream, std::vector<std::string> &values)
{
  std::uint32_t element;
  std::uint64_t count;

  if (!read_exact(stream, &element, sizeof(element)) ||
      !read_exact(stream, &count, sizeof(count)) ||
      element != GGUF_STRING || count > UINT32_MAX)
    {
      return false;
    }

  values.clear();
  values.reserve(static_cast<std::size_t>(count));
  for (std::uint64_t index = 0; index < count; index++)
    {
      std::string value;
      if (!read_string(stream, value))
        {
          return false;
        }

      values.push_back(std::move(value));
    }

  return true;
}

struct vocabulary
{
  std::vector<std::string> tokens;
  std::unordered_map<std::string, std::uint32_t> token_ids;
  std::unordered_map<std::string, std::uint32_t> merge_ranks;

  bool load(const char *path)
  {
    FILE *stream = std::fopen(path, "rb");
    std::vector<std::string> merges;
    std::uint64_t tensor_count;
    std::uint64_t metadata_count;
    std::uint32_t version;
    std::uint32_t magic;
    bool okay = false;

    if (stream == nullptr)
      {
        std::fprintf(stderr, "aipetllm: encode cannot open %s: %d (%s)\n",
                     path, errno, std::strerror(errno));
        return false;
      }

    if (!read_exact(stream, &magic, sizeof(magic)) ||
        !read_exact(stream, &version, sizeof(version)) ||
        !read_exact(stream, &tensor_count, sizeof(tensor_count)) ||
        !read_exact(stream, &metadata_count, sizeof(metadata_count)) ||
        magic != GGUF_MAGIC || version < 2 || version > 3)
      {
        std::fputs("aipetllm: encode invalid GGUF header\n", stderr);
        std::fclose(stream);
        return false;
      }

    for (std::uint64_t index = 0; index < metadata_count; index++)
      {
        std::string key;
        std::uint32_t type;

        if (!read_string(stream, key) ||
            key.size() >= KEY_LIMIT ||
            !read_exact(stream, &type, sizeof(type)))
          {
            std::fputs("aipetllm: encode malformed metadata\n", stderr);
            goto out;
          }

        if (key == "tokenizer.ggml.tokens" && type == GGUF_ARRAY)
          {
            if (!read_string_array(stream, tokens))
              {
                goto out;
              }
          }
        else if (key == "tokenizer.ggml.merges" && type == GGUF_ARRAY)
          {
            if (!read_string_array(stream, merges))
              {
                goto out;
              }
          }
        else if (!skip_value(stream, type))
          {
            goto out;
          }
      }

    if (tokens.empty() || merges.empty())
      {
        std::fputs("aipetllm: encode missing token/merge arrays\n", stderr);
        goto out;
      }

    token_ids.reserve(tokens.size() * 2);
    for (std::size_t index = 0; index < tokens.size(); index++)
      {
        token_ids.emplace(tokens[index], static_cast<std::uint32_t>(index));
      }

    merge_ranks.reserve(merges.size() * 2);
    for (std::size_t index = 0; index < merges.size(); index++)
      {
        merge_ranks.emplace(std::move(merges[index]),
                            static_cast<std::uint32_t>(index));
      }

    okay = true;

out:
    std::fclose(stream);
    return okay;
  }
};

std::size_t utf8_length(unsigned char first)
{
  if (first < 0x80)
    {
      return 1;
    }

  if ((first & 0xe0) == 0xc0)
    {
      return 2;
    }

  if ((first & 0xf0) == 0xe0)
    {
      return 3;
    }

  if ((first & 0xf8) == 0xf0)
    {
      return 4;
    }

  return 0;
}

void append_utf8(std::string &text, std::uint32_t codepoint)
{
  if (codepoint <= 0x7f)
    {
      text.push_back(static_cast<char>(codepoint));
    }
  else if (codepoint <= 0x7ff)
    {
      text.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
      text.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
  else
    {
      text.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
      text.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
      text.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
}

std::uint32_t byte_codepoint(unsigned int byte)
{
  if ((byte >= 0x21 && byte <= 0x7e) ||
      (byte >= 0xa1 && byte <= 0xac) ||
      (byte >= 0xae && byte <= 0xff))
    {
      return byte;
    }

  std::uint32_t replacement = 256;
  for (unsigned int candidate = 0; candidate < byte; candidate++)
    {
      if (!((candidate >= 0x21 && candidate <= 0x7e) ||
            (candidate >= 0xa1 && candidate <= 0xac) ||
            (candidate >= 0xae && candidate <= 0xff)))
        {
          replacement++;
        }
    }

  return replacement;
}

std::string byte_encode(const char *data, std::size_t length)
{
  std::string encoded;
  encoded.reserve(length * 2);
  for (std::size_t index = 0; index < length; index++)
    {
      append_utf8(encoded, byte_codepoint(
        static_cast<unsigned char>(data[index])));
    }

  return encoded;
}

std::vector<std::string> split_symbols(const std::string &text)
{
  std::vector<std::string> result;
  for (std::size_t offset = 0; offset < text.size();)
    {
      std::size_t length = utf8_length(
        static_cast<unsigned char>(text[offset]));
      if (length == 0 || offset + length > text.size())
        {
          result.clear();
          return result;
        }

      result.emplace_back(text, offset, length);
      offset += length;
    }

  return result;
}

bool decode_codepoint(const std::string &text, std::size_t offset,
                      std::uint32_t &codepoint, std::size_t &length)
{
  const auto first = static_cast<unsigned char>(text[offset]);
  length = utf8_length(first);
  if (length == 0 || offset + length > text.size())
    {
      return false;
    }

  if (length == 1)
    {
      codepoint = first;
      return true;
    }

  codepoint = first & (0x7f >> length);
  for (std::size_t index = 1; index < length; index++)
    {
      const auto byte = static_cast<unsigned char>(text[offset + index]);
      if ((byte & 0xc0) != 0x80)
        {
          return false;
        }

      codepoint = (codepoint << 6) | (byte & 0x3f);
    }

  return true;
}

bool is_space(std::uint32_t codepoint)
{
  return codepoint == ' ' || codepoint == '\t' || codepoint == '\n' ||
         codepoint == '\r' || codepoint == '\f' || codepoint == '\v' ||
         codepoint == 0x85 || codepoint == 0xa0 || codepoint == 0x1680 ||
         (codepoint >= 0x2000 && codepoint <= 0x200a) ||
         codepoint == 0x2028 || codepoint == 0x2029 ||
         codepoint == 0x202f || codepoint == 0x205f ||
         codepoint == 0x3000;
}

bool is_newline(std::uint32_t codepoint)
{
  return codepoint == '\r' || codepoint == '\n';
}

bool is_number(std::uint32_t codepoint)
{
  return (codepoint >= '0' && codepoint <= '9') ||
         (codepoint >= 0xff10 && codepoint <= 0xff19);
}

bool is_nonletter_range(std::uint32_t codepoint)
{
  return (codepoint >= 0x2000 && codepoint <= 0x206f) ||
         (codepoint >= 0x2190 && codepoint <= 0x2bff) ||
         (codepoint >= 0x3000 && codepoint <= 0x303f) ||
         (codepoint >= 0xfe10 && codepoint <= 0xfe1f) ||
         (codepoint >= 0xfe30 && codepoint <= 0xfe6f) ||
         (codepoint >= 0xff01 && codepoint <= 0xff0f) ||
         (codepoint >= 0xff1a && codepoint <= 0xff20) ||
         (codepoint >= 0xff3b && codepoint <= 0xff40) ||
         (codepoint >= 0xff5b && codepoint <= 0xff65) ||
         (codepoint >= 0x1f000 && codepoint <= 0x1faff);
}

bool is_letter(std::uint32_t codepoint)
{
  if ((codepoint >= 'A' && codepoint <= 'Z') ||
      (codepoint >= 'a' && codepoint <= 'z'))
    {
      return true;
    }

  if (codepoint < 0x80 || is_space(codepoint) || is_number(codepoint) ||
      is_nonletter_range(codepoint))
    {
      return false;
    }

  /*
   * Qwen2's target languages are covered by treating remaining assigned
   * non-ASCII code points as letters.  Stage 8 golden tests include CJK.
   * A generated Unicode category table will replace this conservative rule
   * before the public tokenizer API is declared complete.
   */

  return true;
}

bool next_unit(const std::string &text, std::size_t offset,
               std::uint32_t &codepoint, std::size_t &length)
{
  return offset < text.size() &&
         decode_codepoint(text, offset, codepoint, length);
}

bool ascii_iequal(char left, char right)
{
  if (left >= 'A' && left <= 'Z')
    {
      left = static_cast<char>(left - 'A' + 'a');
    }

  if (right >= 'A' && right <= 'Z')
    {
      right = static_cast<char>(right - 'A' + 'a');
    }

  return left == right;
}

std::size_t contraction_length(const std::string &text, std::size_t offset)
{
  static const char *suffixes[] = {"'re", "'ve", "'ll", "'s",
                                   "'t", "'m", "'d"};

  if (text[offset] != '\'')
    {
      return 0;
    }

  for (const auto suffix : suffixes)
    {
      std::size_t length = std::strlen(suffix);
      if (offset + length > text.size())
        {
          continue;
        }

      std::size_t index;
      for (index = 0; index < length; index++)
        {
          if (!ascii_iequal(text[offset + index], suffix[index]))
            {
              break;
            }
        }

      if (index == length)
        {
          return length;
        }
    }

  return 0;
}

std::vector<std::string> qwen2_pretokenize(const std::string &text)
{
  std::vector<std::string> spans;
  std::size_t offset = 0;

  while (offset < text.size())
    {
      std::size_t start = offset;
      std::uint32_t codepoint;
      std::size_t length;
      std::size_t contraction = contraction_length(text, offset);

      if (contraction != 0)
        {
          offset += contraction;
          spans.emplace_back(text, start, offset - start);
          continue;
        }

      if (!next_unit(text, offset, codepoint, length))
        {
          spans.clear();
          return spans;
        }

      std::uint32_t after_codepoint = 0;
      std::size_t after_length = 0;
      bool prefix_letter = !is_newline(codepoint) &&
                           !is_letter(codepoint) &&
                           !is_number(codepoint) &&
                           next_unit(text, offset + length,
                                     after_codepoint, after_length) &&
                           is_letter(after_codepoint);
      bool prefix_punctuation = codepoint == ' ' &&
                                next_unit(text, offset + length,
                                          after_codepoint, after_length) &&
                                !is_space(after_codepoint) &&
                                !is_letter(after_codepoint) &&
                                !is_number(after_codepoint);

      if (is_letter(codepoint) || prefix_letter)
        {
          if (prefix_letter)
            {
              offset += length;
            }

          while (next_unit(text, offset, codepoint, length) &&
                 is_letter(codepoint))
            {
              offset += length;
            }
        }
      else if (is_number(codepoint))
        {
          offset += length;
        }
      else if (prefix_punctuation)
        {
          offset += length;
          while (next_unit(text, offset, codepoint, length) &&
                 !is_space(codepoint) && !is_letter(codepoint) &&
                 !is_number(codepoint))
            {
              offset += length;
            }

          while (next_unit(text, offset, codepoint, length) &&
                 is_newline(codepoint))
            {
              offset += length;
            }
        }
      else if (is_space(codepoint))
        {
          bool saw_newline = false;
          while (next_unit(text, offset, codepoint, length) &&
                 is_space(codepoint))
            {
              saw_newline = saw_newline || is_newline(codepoint);
              offset += length;
              if (saw_newline && !is_newline(codepoint))
                {
                  break;
                }
            }
        }
      else
        {
          offset += length;
          while (next_unit(text, offset, codepoint, length) &&
                 !is_space(codepoint) && !is_letter(codepoint) &&
                 !is_number(codepoint))
            {
              offset += length;
            }

          while (next_unit(text, offset, codepoint, length) &&
                 is_newline(codepoint))
            {
              offset += length;
            }
        }

      spans.emplace_back(text, start, offset - start);
    }

  return spans;
}

bool bpe_word(const vocabulary &vocab, const std::string &raw,
              std::vector<std::uint32_t> &output)
{
  std::vector<std::string> symbols = split_symbols(
    byte_encode(raw.data(), raw.size()));

  if (symbols.empty() && !raw.empty())
    {
      return false;
    }

  while (symbols.size() > 1)
    {
      std::uint32_t best_rank = UINT32_MAX;
      std::size_t best_index = symbols.size();

      for (std::size_t index = 0; index + 1 < symbols.size(); index++)
        {
          std::string pair = symbols[index] + " " + symbols[index + 1];
          auto found = vocab.merge_ranks.find(pair);
          if (found != vocab.merge_ranks.end() && found->second < best_rank)
            {
              best_rank = found->second;
              best_index = index;
            }
        }

      if (best_index == symbols.size())
        {
          break;
        }

      symbols[best_index] += symbols[best_index + 1];
      symbols.erase(symbols.begin() +
                    static_cast<std::ptrdiff_t>(best_index + 1));
    }

  for (const auto &symbol : symbols)
    {
      auto found = vocab.token_ids.find(symbol);
      if (found == vocab.token_ids.end())
        {
          std::fprintf(stderr,
                       "aipetllm: BPE produced an unknown token (%lu bytes)\n",
                       static_cast<unsigned long>(symbol.size()));
          return false;
        }

      output.push_back(found->second);
    }

  return true;
}

}

static int encode_with_vocab(vocabulary &vocab,
                          const char *path, const char *prompt,
                          std::uint32_t *output, std::uint32_t capacity,
                          std::uint32_t *count)
{
  std::vector<std::uint32_t> tokens;
  std::vector<std::string> spans;

  if (prompt == nullptr || prompt[0] == '\0')
    {
      std::fputs("aipetllm: encode prompt must not be empty\n", stderr);
      return 1;
    }

  spans = qwen2_pretokenize(prompt);
  if (spans.empty())
    {
      std::fputs("aipetllm: Qwen2 pre-tokenizer rejected prompt\n", stderr);
      return 1;
    }

  for (const auto &span : spans)
    {
      if (!bpe_word(vocab, span, tokens))
        {
          return 1;
        }

      if (output != nullptr && tokens.size() > capacity)
        {
          std::fputs("aipetllm: prompt exceeds token capacity\n", stderr);
          return 1;
        }
    }

  if (output != nullptr)
    {
      *count = static_cast<std::uint32_t>(tokens.size());
      for (std::size_t index = 0; index < tokens.size(); index++)
        {
          output[index] = tokens[index];
        }
    }

  std::printf("qwen2-bpe path=%s vocab=%lu merges=%lu prompt-bytes=%lu "
              "spans=%lu\n",
              path, static_cast<unsigned long>(vocab.tokens.size()),
              static_cast<unsigned long>(vocab.merge_ranks.size()),
              static_cast<unsigned long>(std::strlen(prompt)),
              static_cast<unsigned long>(spans.size()));
  std::printf("tokens=%lu ids=", static_cast<unsigned long>(tokens.size()));
  for (std::size_t index = 0; index < tokens.size(); index++)
    {
      std::printf("%s%" PRIu32, index == 0 ? "" : ",", tokens[index]);
    }

  std::putchar('\n');
  std::puts("Qwen2 pre-tokenizer and byte-level BPE checkpoint passed; "
            "full Unicode category audit pending.");
  return 0;
}

static int encode_tokens(const char *path, const char *prompt,
                          std::uint32_t *output, std::uint32_t capacity,
                          std::uint32_t *count)
{
  vocabulary vocab;
  if (!vocab.load(path))
    {
      return 1;
    }

  return encode_with_vocab(vocab, path, prompt, output, capacity, count);
}

/* Single-turn ChatML: special tokens are looked up and appended as IDs,
 * never passed through ordinary byte-level BPE. User text remains ordinary
 * text; control-token spellings are rejected instead of being interpreted.
 */
extern "C" int aipetllm_chat_system_tokens(const char *path, const char *system,
                                    const char *prompt,
                                    std::uint32_t *output,
                                    std::uint32_t capacity,
                                    std::uint32_t *count,
                                    std::uint32_t *stop_token)
{
  vocabulary vocab;
  if (system == nullptr || system[0] == '\0' ||
      std::strlen(system) > 4096 || std::strstr(system, "<|") != nullptr ||
      prompt == nullptr || prompt[0] == '\0' ||
      std::strlen(prompt) > 4096 || std::strstr(prompt, "<|") != nullptr ||
      output == nullptr || count == nullptr || stop_token == nullptr ||
      capacity == 0 || !vocab.load(path))
    {
      return 1;
    }

  auto start = vocab.token_ids.find("<|im_start|>");
  auto end = vocab.token_ids.find("<|im_end|>");
  if (start == vocab.token_ids.end() || end == vocab.token_ids.end())
    {
      std::fputs("aipetllm: ChatML special tokens missing\n", stderr);
      return 1;
    }

  *count = 0;
  *stop_token = end->second;
  auto special = [&](std::uint32_t id) -> bool
    {
      if (*count >= capacity) return false;
      output[(*count)++] = id;
      return true;
    };
  auto ordinary = [&](const char *text) -> bool
    {
      std::uint32_t written = 0;
      if (*count >= capacity ||
          encode_with_vocab(vocab, path, text, output + *count,
                            capacity - *count, &written) != 0) return false;
      *count += written;
      return true;
    };

  if (!special(start->second) ||
      !ordinary((std::string("system\n") + system).c_str()) ||
      !special(end->second) || !ordinary("\n") ||
      !special(start->second) || !ordinary("user\n") ||
      !ordinary(prompt) || !special(end->second) || !ordinary("\n") ||
      !special(start->second) || !ordinary("assistant\n"))
    {
      std::fputs("aipetllm: ChatML prompt exceeds token capacity\n", stderr);
      *count = 0;
      return 1;
    }

  std::printf("chatml tokens=%" PRIu32 " start=%" PRIu32
              " stop=%" PRIu32 " single-turn=1\n", *count,
              start->second, end->second);
  return 0;
}

extern "C" int aipetllm_encode_tokens(const char *path, const char *prompt,
                                      std::uint32_t *output,
                                      std::uint32_t capacity,
                                      std::uint32_t *count)
{
  if (output == nullptr || count == nullptr || capacity == 0 ||
      prompt == nullptr || std::strlen(prompt) > 4096)
    {
      return 1;
    }

  *count = 0;
  return encode_tokens(path, prompt, output, capacity, count);
}

extern "C" int aipetllm_chat_tokens(const char *path, const char *prompt,
                                    std::uint32_t *output, std::uint32_t capacity,
                                    std::uint32_t *count, std::uint32_t *stop)
{
  return aipetllm_chat_system_tokens(path, "You are a helpful assistant.",
                                    prompt, output, capacity, count, stop);
}

struct token_stream
{
  std::vector<std::string> pieces;
  std::string pending;
  int inverse[512];
  int (*sink)(void *, const char *, std::size_t) = nullptr;
  void *sink_context = nullptr;
};

extern "C" void *aipetllm_stream_open(const char *path)
{
  vocabulary vocab;
  if (!vocab.load(path)) return nullptr;
  auto *stream = new token_stream;
  stream->pieces.swap(vocab.tokens);
  for (unsigned index = 0; index < 512; index++) stream->inverse[index] = -1;
  for (unsigned byte = 0; byte < 256; byte++)
    stream->inverse[byte_codepoint(byte)] = static_cast<int>(byte);
  return stream;
}

extern "C" int aipetllm_stream_emit(void *context, std::uint32_t token)
{
  auto *stream = static_cast<token_stream *>(context);
  if (stream == nullptr || token >= stream->pieces.size()) return 1;
  const auto &piece = stream->pieces[token];
  for (std::size_t offset = 0; offset < piece.size();)
    {
      std::uint32_t codepoint;
      std::size_t length;
      if (!next_unit(piece, offset, codepoint, length) || codepoint >= 512 ||
          stream->inverse[codepoint] < 0) return 1;
      stream->pending.push_back(static_cast<char>(stream->inverse[codepoint]));
      offset += length;
    }

  /* Byte-level BPE can split a Chinese UTF-8 character across tokens. Hold
   * an incomplete trailing character, avoiding diagnostics between bytes.
   */
  std::size_t complete = 0;
  while (complete < stream->pending.size())
    {
      std::uint32_t codepoint;
      std::size_t length;
      if (!next_unit(stream->pending, complete, codepoint, length)) break;
      complete += length;
    }
  if (complete != 0)
    {
      if (stream->sink != nullptr)
        {
          if (stream->sink(stream->sink_context, stream->pending.data(), complete))
            return 1;
        }
      else
        {
          if (std::fwrite(stream->pending.data(), 1, complete, stdout) != complete)
            return 1;
          std::fflush(stdout);
        }
      stream->pending.erase(0, complete);
    }
  if (stream->pending.size() > 4) return 1;
  return 0;
}

extern "C" void aipetllm_stream_close(void *context)
{
  auto *stream = static_cast<token_stream *>(context);
  if (stream != nullptr && stream->sink == nullptr && !stream->pending.empty())
    {
      std::fputs("\xef\xbf\xbd", stdout);
      std::fputs("\naipetllm: incomplete UTF-8 tail replaced\n", stderr);
    }
  delete stream;
}

extern "C" void *aipetllm_stream_sink_open(const char *path,
    int (*sink)(void *, const char *, std::size_t), void *context)
{
  if (sink == nullptr) return nullptr;
  auto *stream = static_cast<token_stream *>(aipetllm_stream_open(path));
  if (stream != nullptr) { stream->sink = sink; stream->sink_context = context; }
  return stream;
}

extern "C" int aipetllm_stream_sink_finish(void *context)
{
  auto *stream = static_cast<token_stream *>(context);
  return stream != nullptr && stream->pending.empty() ? 0 : 1;
}

#ifdef AIPETLLM_BPE_TEST_API
extern "C" int aipetllm_stream_split_test(const char *path)
{
  vocabulary vocab;
  if (!vocab.load(path)) return 1;
  auto *stream = static_cast<token_stream *>(aipetllm_stream_open(path));
  if (stream == nullptr) return 1;
  const unsigned char bytes[] = {0xe4, 0xbd, 0xa0};
  int result = 0;
  for (unsigned index = 0; index < 3; index++)
    {
      char byte = static_cast<char>(bytes[index]);
      auto token = vocab.token_ids.find(byte_encode(&byte, 1));
      if (token == vocab.token_ids.end() ||
          aipetllm_stream_emit(stream, token->second) != 0 ||
          stream->pending.size() != (index == 2 ? 0 : index + 1))
        {
          result = 1;
          break;
        }
    }
  aipetllm_stream_close(stream);
  return result;
}
#endif

extern "C" int aipetllm_chat_continue_tokens(const char *path,
 const char *prompt, const std::uint32_t *history, std::uint32_t history_count,
 std::uint32_t *output, std::uint32_t capacity, std::uint32_t *count,
 std::uint32_t *stop_token)
{
  vocabulary vocab;
  if (prompt == nullptr || prompt[0] == '\0' ||
      std::strlen(prompt) > 4096 || std::strstr(prompt, "<|") != nullptr ||
      history == nullptr || output == nullptr || count == nullptr ||
      stop_token == nullptr || history_count == 0 || history_count >= capacity ||
      !vocab.load(path)) return 1;
  auto start = vocab.token_ids.find("<|im_start|>");
  auto end = vocab.token_ids.find("<|im_end|>");
  if (start == vocab.token_ids.end() || end == vocab.token_ids.end() ||
      history[history_count - 1] != end->second) return 1;
  for (std::uint32_t index = 0; index < history_count; index++)
    {
      if (history[index] >= vocab.tokens.size()) return 1;
      output[index] = history[index];
    }
  *count = history_count;
  *stop_token = end->second;
  auto ordinary = [&](const char *text) -> bool
    {
      std::uint32_t written = 0;
      if (*count >= capacity || encode_with_vocab(vocab, path, text,
          output + *count, capacity - *count, &written) != 0) return false;
      *count += written;
      return true;
    };
  auto special = [&](std::uint32_t token) -> bool
    {
      if (*count >= capacity) return false;
      output[(*count)++] = token;
      return true;
    };
  if (!ordinary("\n") || !special(start->second) || !ordinary("user\n") ||
      !ordinary(prompt) || !special(end->second) || !ordinary("\n") ||
      !special(start->second) || !ordinary("assistant\n"))
    {
      *count = 0;
      std::fputs("aipetllm: history full; use newchat\n", stderr);
      return 1;
    }
  std::printf("chatml tokens=%" PRIu32 " history=%" PRIu32 "\n",
              *count, history_count);
  return 0;
}

extern "C" int aipetllm_bpe_checkpoint(const char *path, const char *prompt)
{
  return encode_tokens(path, prompt, nullptr, 0, nullptr);
}

#ifdef AIPETLLM_BPE_STANDALONE
int main(int argc, char **argv)
{
  if (argc != 3)
    {
      std::fprintf(stderr, "usage: %s MODEL.gguf PROMPT\n", argv[0]);
      return 2;
    }

  return aipetllm_bpe_checkpoint(argv[1], argv[2]);
}
#endif

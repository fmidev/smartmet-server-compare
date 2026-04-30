#include "UrlUtils.h"

#include <string>

static int hex_digit(char c)
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

std::string urldecode(const std::string& input)
{
  std::string out;
  out.reserve(input.size());

  for (std::size_t i = 0; i < input.size(); ++i)
  {
    if (input[i] == '%' && i + 2 < input.size())
    {
      int hi = hex_digit(input[i + 1]);
      int lo = hex_digit(input[i + 2]);
      if (hi >= 0 && lo >= 0)
      {
        out += static_cast<char>(hi * 16 + lo);
        i += 2;
        continue;
      }
    }
    else if (input[i] == '+')
    {
      out += ' ';
      continue;
    }
    out += input[i];
  }
  return out;
}

std::string urlencode(const std::string& input)
{
  static const char kHex[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(input.size() * 3);
  for (auto uc = reinterpret_cast<const unsigned char*>(input.data()),
            end = uc + input.size(); uc != end; ++uc)
  {
    const unsigned char c = *uc;
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~')
    {
      out += static_cast<char>(c);
    }
    else
    {
      out += '%';
      out += kHex[(c >> 4) & 0x0F];
      out += kHex[c & 0x0F];
    }
  }
  return out;
}

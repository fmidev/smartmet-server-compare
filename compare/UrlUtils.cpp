#include "UrlUtils.h"

#include <cctype>

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

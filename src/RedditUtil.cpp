#include "RedditUtil.h"

namespace reddit {
namespace util {

static const char kB64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(const uint8_t* data, size_t len) {
  std::string out;
  out.reserve(((len + 2) / 3) * 4);
  for (size_t i = 0; i < len; i += 3) {
    uint32_t n = (uint32_t)data[i] << 16;
    if (i + 1 < len) n |= (uint32_t)data[i + 1] << 8;
    if (i + 2 < len) n |= data[i + 2];
    out.push_back(kB64[(n >> 18) & 63]);
    out.push_back(kB64[(n >> 12) & 63]);
    out.push_back(i + 1 < len ? kB64[(n >> 6) & 63] : '=');
    out.push_back(i + 2 < len ? kB64[n & 63] : '=');
  }
  return out;
}

static bool isUnreserved(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
         (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
         c == '~';
}

static void appendPct(std::string& out, char c, bool spaceAsPlus) {
  static const char* hex = "0123456789ABCDEF";
  if (c == ' ' && spaceAsPlus) {
    out.push_back('+');
    return;
  }
  out.push_back('%');
  out.push_back(hex[(c >> 4) & 15]);
  out.push_back(hex[c & 15]);
}

std::string formEncode(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    if (isUnreserved(c))
      out.push_back(c);
    else
      appendPct(out, c, true);
  }
  return out;
}

std::string urlEncode(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    if (isUnreserved(c))
      out.push_back(c);
    else
      appendPct(out, c, false);
  }
  return out;
}

std::string buildFormBody(
    const std::initializer_list<std::pair<std::string, std::string>>& params) {
  std::string out;
  bool first = true;
  for (const auto& p : params) {
    if (!first) out.push_back('&');
    first = false;
    out += formEncode(p.first);
    out.push_back('=');
    out += formEncode(p.second);
  }
  return out;
}

}  // namespace util
}  // namespace reddit

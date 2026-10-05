#pragma once
// Pure helpers: base64 and URL/form encoding. No Arduino dependency,
// fully unit-testable on the host.
#include <string>
#include <stdint.h>

namespace reddit {
namespace util {

// Standard base64 (with padding), for the OAuth Basic auth header.
std::string base64Encode(const uint8_t* data, size_t len);
inline std::string base64Encode(const std::string& s) {
  return base64Encode(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

// application/x-www-form-urlencoded encoding for POST bodies.
// Encodes everything except [A-Za-z0-9-_.~]; space becomes '+'.
std::string formEncode(const std::string& s);

// Percent-encoding for URL path/query segments. Space becomes %20.
std::string urlEncode(const std::string& s);

// Build "a=1&b=2" from pairs, form-encoding keys and values.
std::string buildFormBody(
    const std::initializer_list<std::pair<std::string, std::string>>& params);

}  // namespace util
}  // namespace reddit

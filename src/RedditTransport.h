#pragma once
// Transport seam. All network I/O goes through ITransport, so the entire
// client logic compiles and runs on the host with a fake transport.
// The ESP32 implementation lives in EspTransport (Arduino only).
#include <string>
#include <vector>
#include <stdint.h>

namespace reddit {

struct HttpHeader {
  std::string name;
  std::string value;
};

struct HttpRequest {
  std::string method;  // "GET" or "POST"
  std::string url;
  std::vector<HttpHeader> headers;
  std::string body;  // form-encoded POST body; empty for GET
  uint32_t timeoutMs = 15000;
};

struct HttpResponse {
  int status = 0;  // HTTP status code; <= 0 means the transport itself failed
  std::string body;
  // Rate-limit headers when the server sent them (may be empty).
  std::string rateLimitUsed;
  std::string rateLimitRemaining;
  std::string rateLimitReset;  // seconds until window reset
  std::string retryAfter;      // seconds, on 429
  bool timeout = false;
};

class ITransport {
 public:
  virtual ~ITransport() = default;
  virtual HttpResponse send(const HttpRequest& req) = 0;
};

}  // namespace reddit

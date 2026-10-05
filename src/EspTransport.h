#pragma once
// ESP32 transport: ITransport implemented with HTTPClient + WiFiClientSecure.
// Arduino-only. Long-lived: construct once, reuse for every request.
//
// TLS trust (in order):
//   1. setCaCert(pem) — pin your own CA (smallest flash cost).
//   2. Default — the arduino-esp32 built-in Mozilla CA bundle
//      (setCACertBundle). Correct, survives CA rotation.
//   3. config.insecureTls = true — skip validation entirely. Debug only.
//
// NOTE: the ESP32 has no RTC. Call configTime()/NTP sync after WiFi
// connects and BEFORE the first request, or cert validation will fail.
#include "RedditTransport.h"
#include "RedditConfig.h"

#ifdef ARDUINO
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

namespace reddit {

class EspTransport : public ITransport {
 public:
  explicit EspTransport(const RedditConfig& config = RedditConfig());

  // Pin a custom CA certificate (PEM, NUL-terminated). Overrides the
  // default bundle. Call before the first send().
  void setCaCert(const char* pem) { _caCert = pem; }

  HttpResponse send(const HttpRequest& req) override;

 private:
  void ensureClients();

  RedditConfig _config;
  const char* _caCert = nullptr;
  WiFiClientSecure _tls;
  HTTPClient _http;
  bool _tlsConfigured = false;
};

}  // namespace reddit

#endif  // ARDUINO

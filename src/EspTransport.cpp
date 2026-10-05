#include "EspTransport.h"

#ifdef ARDUINO
#include <esp_crt_bundle.h>

namespace reddit {

EspTransport::EspTransport(const RedditConfig& config) : _config(config) {}

void EspTransport::ensureClients() {
  if (_tlsConfigured) return;
  _tlsConfigured = true;
  if (_config.insecureTls) {
    _tls.setInsecure();
  } else if (_caCert) {
    _tls.setCACert(_caCert);
  } else {
    // Built-in Mozilla bundle shipped with arduino-esp32.
    _tls.setCACertBundle(arduino_esp_crt_bundle_attach);
  }
  _tls.setHandshakeTimeout(10);
}

HttpResponse EspTransport::send(const HttpRequest& req) {
  ensureClients();
  HttpResponse res;

  _http.begin(_tls, req.url.c_str());
  // Reddit serves chunked responses; HTTPClient's chunked decoder is
  // unreliable, so force HTTP/1.0 (Content-Length framing instead).
  _http.useHTTP10(true);
  _http.setTimeout(req.timeoutMs > 0 ? req.timeoutMs : _config.httpTimeoutMs);
  for (const auto& h : req.headers) {
    _http.addHeader(h.name.c_str(), h.value.c_str());
  }

  int code;
  if (req.method == "POST") {
    code = _http.POST((uint8_t*)req.body.data(), req.body.size());
  } else {
    code = _http.GET();
  }

  if (code < 0) {
    // Transport-level failure (DNS, TCP, TLS, timeout). Not an HTTP status.
    res.status = code;
    res.timeout = (code == HTTPC_ERROR_READ_TIMEOUT ||
                   code == HTTPC_ERROR_CONNECTION_TIMEOUT);
  } else {
    res.status = code;
    res.body = _http.getString().c_str();
    res.rateLimitUsed = _http.header("x-ratelimit-used").c_str();
    res.rateLimitRemaining = _http.header("x-ratelimit-remaining").c_str();
    res.rateLimitReset = _http.header("x-ratelimit-reset").c_str();
    res.retryAfter = _http.header("retry-after").c_str();
  }
  _http.end();  // must run after every request; frees connection state
  return res;
}

}  // namespace reddit

#endif  // ARDUINO

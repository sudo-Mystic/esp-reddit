#include "EspTransport.h"

#ifdef ARDUINO
#include "RedditRootCA.h"

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
    // Curated root set covering Reddit's observed issuers (GlobalSign,
    // Google Trust Services, DigiCert, Let's Encrypt). ~11KB flash,
    // versus ~200KB for the full Mozilla bundle.
    _tls.setCACert(REDDIT_ROOT_CA_PEM);
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
  // header() only returns headers registered here; without this the
  // rate-limit tracking below would silently see empty strings.
  const char* collectKeys[] = {
      "x-ratelimit-used", "x-ratelimit-remaining", "x-ratelimit-reset",
      "retry-after"};
  _http.collectHeaders(collectKeys, 4);
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
    res.timeout = (code == HTTPC_ERROR_READ_TIMEOUT);
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

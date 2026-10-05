#include "RedditAuth.h"

#include <ArduinoJson.h>

#include "RedditTime.h"
#include "RedditUtil.h"

namespace reddit {

namespace {
constexpr const char* kTokenUrl = "https://www.reddit.com/api/v1/access_token";
}  // namespace

bool RedditAuth::configure(const std::string& clientId,
                           const std::string& clientSecret,
                           const std::string& username,
                           const std::string& password,
                           const std::string& userAgent) {
  if (clientId.empty() || clientSecret.empty() || username.empty() ||
      password.empty() || userAgent.empty()) {
    return false;
  }
  _clientId = clientId;
  _clientSecret = clientSecret;
  _username = username;
  _password = password;
  _userAgent = userAgent;
  _configured = true;
  // Credentials changed: any cached token belongs to the old identity.
  invalidate();
  _lastError = RedditError::Ok;
  _lastMessage.clear();
  return true;
}

bool RedditAuth::ensureToken(ITransport& transport, uint32_t marginSec) {
  if (!_configured) {
    _lastError = RedditError::InvalidArgs;
    _lastMessage = "RedditAuth::configure() was never called";
    return false;
  }
  if (!_token.empty()) {
    // Valid while nowMs() < obtainedAt + (expiresIn - margin) * 1000.
    // Written wrap-safe: (now - obtained) < validMs. Clamp the window to
    // zero when margin >= expiresIn so the token is treated as expired.
    const uint64_t validMs = (_expiresInSec > marginSec)
                                 ? static_cast<uint64_t>(_expiresInSec - marginSec) * 1000u
                                 : 0u;
    if (static_cast<uint64_t>(nowMs() - _obtainedAtMs) < validMs) {
      _lastError = RedditError::Ok;
      _lastMessage.clear();
      return true;
    }
  }
  return requestToken(transport);
}

std::string RedditAuth::authorizationHeader() const {
  if (_token.empty()) return {};
  return "Bearer " + _token;
}

void RedditAuth::invalidate() {
  _token.clear();
  _obtainedAtMs = 0;
  _expiresInSec = 0;
}

bool RedditAuth::requestToken(ITransport& transport) {
  _lastError = RedditError::Ok;
  _lastMessage.clear();

  HttpRequest req;
  req.method = "POST";
  req.url = kTokenUrl;
  req.timeoutMs = 15000;
  req.headers.push_back(
      {"Authorization",
       "Basic " + util::base64Encode(_clientId + ":" + _clientSecret)});
  req.headers.push_back(
      {"Content-Type", "application/x-www-form-urlencoded"});
  // Reddit 429s token requests with a missing/generic User-Agent.
  req.headers.push_back({"User-Agent", _userAgent});
  req.body = util::buildFormBody({
      {"grant_type", "password"},
      {"username", _username},
      {"password", _password},
  });

  const HttpResponse resp = transport.send(req);

  if (resp.status <= 0) {
    _lastError =
        resp.timeout ? RedditError::HttpTimeout : RedditError::TransportError;
    _lastMessage = "token request failed below HTTP";
    _token.clear();
    return false;
  }
  if (resp.status == 401) {
    // Bad client_id/client_secret in the Basic auth header.
    _lastError = RedditError::AuthFailed;
    _lastMessage = "invalid client credentials";
    _token.clear();
    return false;
  }
  if (resp.status == 429) {
    _lastError = RedditError::RateLimited;
    _lastMessage = "token endpoint rate limited (HTTP 429)";
    return false;
  }
  if (resp.status != 200) {
    _lastError = RedditError::HttpError;
    _lastMessage =
        "token endpoint returned HTTP " + std::to_string(resp.status);
    _token.clear();
    return false;
  }
  uint32_t expiresIn = 0;
  if (!parseTokenResponse(resp.body, expiresIn)) {
    _token.clear();
    return false;  // _lastError/_lastMessage already set
  }
  _expiresInSec = expiresIn;
  _obtainedAtMs = nowMs();
  return true;
}

bool RedditAuth::parseTokenResponse(const std::string& body,
                                    uint32_t& expiresIn) {
  JsonDocument doc;  // tiny response: no filter needed
  const DeserializationError err =
      deserializeJson(doc, body.c_str(), body.size());
  if (err) {
    _lastError = RedditError::JsonError;
    _lastMessage = std::string("token response JSON error: ") + err.c_str();
    return false;
  }
  // {"error": "invalid_grant"}: wrong username/password, or 2FA enabled.
  const char* apiError = doc["error"] | "";
  if (apiError[0] != '\0') {
    _lastError = RedditError::AuthFailed;
    _lastMessage = apiError;
    return false;
  }
  const char* token = doc["access_token"] | "";
  if (token[0] == '\0') {
    _lastError = RedditError::AuthFailed;
    _lastMessage = "token response missing access_token";
    return false;
  }
  // Reddit documents 3600; fall back to it if the field is absent rather
  // than caching a token that looks instantly expired.
  expiresIn = doc["expires_in"] | 3600u;
  _token = token;
  return true;
}

}  // namespace reddit

#pragma once
// OAuth2 "script" app token management. Handles the password grant against
// https://www.reddit.com/api/v1/access_token, caches the bearer token, and
// re-authenticates proactively before expiry (tracked with reddit::nowMs(),
// so it works without an RTC and is unit-testable).
#include <string>
#include <stdint.h>
#include "RedditTransport.h"
#include "RedditError.h"

namespace reddit {

class RedditAuth {
 public:
  RedditAuth() = default;

  // All fields required. Returns false if any is empty.
  bool configure(const std::string& clientId, const std::string& clientSecret,
                 const std::string& username, const std::string& password,
                 const std::string& userAgent);

  bool isConfigured() const { return _configured; }

  // Ensures a valid token, running the password grant when missing or
  // expiring within marginSec. Returns false on failure (see lastError()).
  bool ensureToken(ITransport& transport, uint32_t marginSec = 300);

  // "Bearer <token>", or empty when no token is held.
  std::string authorizationHeader() const;
  const std::string& userAgent() const { return _userAgent; }

  // Drop the cached token (call after an HTTP 401 so the next request
  // re-authenticates instead of retrying with a dead token).
  void invalidate();

  RedditError lastError() const { return _lastError; }
  const std::string& lastErrorMessage() const { return _lastMessage; }

 private:
  bool requestToken(ITransport& transport);
  bool parseTokenResponse(const std::string& body, uint32_t& expiresIn);

  bool _configured = false;
  std::string _clientId, _clientSecret, _username, _password, _userAgent;
  std::string _token;
  uint32_t _obtainedAtMs = 0;
  uint32_t _expiresInSec = 0;
  RedditError _lastError = RedditError::Ok;
  std::string _lastMessage;
};

}  // namespace reddit

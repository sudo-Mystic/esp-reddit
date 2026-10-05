#pragma once
// Error taxonomy for esp-reddit. Every public API sets lastError();
// callers (and tests) can rely on these distinct cases.
#include <string>

namespace reddit {

enum class RedditError {
  Ok = 0,
  NotInitialized,   // begin() was never called (or failed)
  InvalidArgs,      // empty/invalid argument passed by the caller
  AuthFailed,       // token request rejected (bad credentials, 2FA, ...)
  TokenRefreshFailed,
  TransportError,   // TCP/TLS/DNS failure below HTTP
  HttpTimeout,
  HttpError,        // other 4xx/5xx; see ErrorInfo::httpStatus
  Unauthorized,     // HTTP 401: bad/expired token (client auto-retries once)
  Forbidden,        // HTTP 403: valid token, action not allowed
  NotFound,         // HTTP 404
  RateLimited,      // HTTP 429 or x-ratelimit budget exhausted
  SpamBlocked,      // RATELIMIT inside json.errors (per-account write cooldown)
  JsonError,        // response wasn't parseable (incl. out-of-memory)
  ApiError,         // json.errors[] non-empty on a mutation
};

const char* errorName(RedditError e);

struct ErrorInfo {
  RedditError code = RedditError::Ok;
  int httpStatus = 0;
  std::string message;
  std::string redditCode;  // e.g. "RATELIMIT", "NO_TEXT" from json.errors
};

}  // namespace reddit

#pragma once
// Tunables for the Reddit client. All limits exist to keep RAM usage
// predictable on a 520KB-SRAM ESP32.
#include <stdint.h>

namespace reddit {

struct RedditConfig {
  // Listing pagination: Reddit allows up to 100, we clamp harder.
  uint8_t defaultLimit = 10;
  uint8_t maxLimit = 25;

  // Client-side pacing between requests (ms). Reddit's OAuth budget is
  // ~100 req/min; 1100ms keeps us comfortably under it.
  uint32_t minRequestIntervalMs = 1100;

  // Per-request HTTP timeout (ms). Applies to connect + response.
  uint32_t httpTimeoutMs = 15000;

  // Max recursion depth when parsing nested comment replies.
  uint8_t maxCommentDepth = 6;

  // Re-authenticate this many seconds before the token actually expires.
  uint32_t tokenRefreshMarginSec = 300;

  // If true, skip TLS certificate validation. Debug only: never ship this.
  bool insecureTls = false;

  // When x-ratelimit-remaining hits this value, pause requests until
  // x-ratelimit-reset seconds pass.
  int rateLimitFloor = 5;
};

}  // namespace reddit

# Setup Guide

## 1. Create a Reddit script app

1. Go to https://www.reddit.com/prefs/apps (log in first).
2. Click **create another app...** at the bottom.
3. Fill in:
   - **name**: anything, e.g. `esp32-bot`
   - **App type**: select **script**
   - **redirect uri**: `http://localhost:8080` (required field, unused by the password grant)
4. Click **create app**.
5. Note two values:
   - **client_id**: the ~14 character string under the app name
   - **client_secret**: the "secret" field

## 2. Account requirements

- Use the account's **username and password** in `begin()`. The script app is locked to the account that created it.
- **Disable 2FA** on that account (or use a dedicated bot account without 2FA). The password grant fails with `invalid_grant` when 2FA is enabled.

## 3. User-Agent format

Every request needs a descriptive User-Agent. Reddit blocks or rate-limits generic ones immediately. Format:

```
<platform>:<app>:<version> (by /u/<username>)
```

Example: `esp32:esp-reddit:0.1.0 (by /u/yourname)`. Put your firmware version in it.

## 4. Wiring

None. The library only needs the ESP32's built-in WiFi.

## 5. WiFi + NTP (required)

The ESP32 has no real-time clock. On boot the clock reads 1970, so every TLS certificate looks expired and handshakes fail. Sync time **after WiFi connects and before the first Reddit call**:

```cpp
#include <WiFi.h>
#include <time.h>

WiFi.begin("YOUR_SSID", "YOUR_PASSWORD");
while (WiFi.status() != WL_CONNECTED) delay(500);

configTime(0, 0, "pool.ntp.org", "time.nist.gov");
time_t now = 0;
for (int i = 0; i < 20 && now < 1700000000; i++) {
  delay(500);
  time(&now);
}
if (now < 1700000000) {
  Serial.println("NTP sync failed, cannot do TLS");
  while (true) delay(1000);
}
```

If NTP fails, stop loudly at startup. Do not proceed to HTTPS calls.

## 6. Rate limits

- **Authenticated (script app):** ~100 queries/minute per `client_id`, averaged over 10 minutes. The client paces requests (`minRequestIntervalMs`, default 1100ms) and backs off on `x-ratelimit-remaining` automatically.
- **Read-only (no auth):** ~30 req/min and ~2000 req/day per source IP, shared with strangers behind carrier NAT. Prefer a script app even for read-only use if you poll regularly.
- **Writes** (submit, comment, vote) have extra per-account spam throttling independent of HTTP limits. A `RATELIMIT` error in the response body means "wait N minutes", not an HTTP 429.
- All devices sharing one `client_id` share one budget. Design polling intervals accordingly.

## 7. Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| TLS handshake fails / `TransportError` on every request | Clock not synced | Run the NTP snippet before the first request |
| `invalid_grant` on `begin()` | Wrong password, or 2FA enabled | Check credentials; disable 2FA on the account |
| HTTP 401 on API calls | Bad/expired token | Client auto-retries once after re-auth; if it persists, check `client_id`/`client_secret` |
| HTTP 403 | Valid token, action forbidden | Private/quarantined subreddit, karma or age gate, or moderator-only endpoint. Do not re-auth; fix the request |
| HTTP 429 on every call | Missing or malformed User-Agent | Check the format in section 3, set it on `begin()`/`beginReadOnly()` |
| `SpamBlocked` / RATELIMIT in error message | Per-account write cooldown | Wait the time stated in the message, then retry |
| `JsonError` / `NoMemory` | Response too large for heap | Reduce `ListingOptions.limit`, or lower `RedditConfig.maxLimit` |
| DNS resolves but connect fails | Captive portal or broken DHCP DNS | Test `WiFi.hostByName("oauth.reddit.com", ip)` to separate DNS failure from TLS failure |
| Works on WiFi, fails on phone hotspot | Carrier NAT rate limits | Read-only budget is shared per IP; switch to authenticated mode |

Never print `client_secret`, passwords, or tokens to Serial in production builds.

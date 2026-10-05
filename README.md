# esp-reddit

Lightweight Reddit API wrapper for ESP32. PRAW-inspired, synchronous, and built for small RAM: listings, submissions, comments, voting, all with OAuth2 and built-in rate limiting.

## Features

- OAuth2 "script" app flow with automatic token refresh
- Read-only mode: no credentials needed for public listings, comments, profiles
- Hot / new / rising / controversial / top listings with pagination
- Submit posts, comment, vote, save, edit, delete, subscribe
- Comment tree fetching with depth-capped parsing
- Client-side rate limiting with `x-ratelimit-*` header backoff
- Memory-safe by design: streaming JSON parse with field filters, capped page sizes, no response buffering
- Host-testable core: all logic behind a transport seam, ESP32 code isolated in `EspTransport`

## Quick start

### Read-only: top 10 hot posts of r/esp32

```cpp
#include <WiFi.h>
#include <esp_reddit.h>

reddit::EspTransport transport;
reddit::RedditClient client;

void setup() {
  Serial.begin(115200);
  WiFi.begin("YOUR_SSID", "YOUR_PASSWORD");
  while (WiFi.status() != WL_CONNECTED) delay(500);

  configTime(0, 0, "pool.ntp.org");  // required for TLS, do this before any request
  time_t now = 0;
  while (now < 1700000000) { delay(500); time(&now); }

  client.beginReadOnly("esp32:my-app:0.1.0 (by /u/yourname)", transport);

  for (const auto& p : client.hot("esp32")) {
    Serial.printf("[%ld] %s (u/%s)\n", p.score, p.title.c_str(), p.author.c_str());
  }
}
void loop() {}
```

### Authenticated: submit a text post

```cpp
reddit::RedditClient client;
reddit::EspTransport transport;

client.begin("YOUR_CLIENT_ID", "YOUR_CLIENT_SECRET",
             "YOUR_REDDIT_USERNAME", "YOUR_REDDIT_PASSWORD",
             "esp32:my-app:0.1.0 (by /u/yourname)", transport);

std::string fullname;
if (client.submitText("test", "Hello from ESP32", "Posted by a microcontroller.", &fullname)) {
  Serial.println(fullname.c_str());  // t3_xxxxxx
} else {
  Serial.printf("failed: %s\n", client.lastErrorMessage().c_str());
}
```

## Installation

### Arduino IDE (Library Manager)

1. Sketch > Include Library > Manage Libraries
2. Search for **esp-reddit**
3. Install (pulls in ArduinoJson automatically)

### PlatformIO

```ini
lib_deps =
  sudo-Mystic/esp-reddit
  bblanchon/ArduinoJson@^7.4.0
```

## Requirements

- ESP32 board (any variant, Arduino framework)
- ArduinoJson 7.x (installed automatically)
- For authenticated calls: a Reddit "script" app (see [docs/SETUP.md](docs/SETUP.md))

## Docs

- [docs/SETUP.md](docs/SETUP.md): Reddit app setup, wiring, WiFi+NTP, troubleshooting
- [docs/API.md](docs/API.md): full API reference

## License

MIT

// ReadHotPosts: read-only demo. Prints the top 10 hot posts of r/esp32.
// No Reddit account needed. Only WiFi credentials are yours.

#include <WiFi.h>
#include <time.h>
#include <esp_reddit.h>

const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

// Format: <platform>:<app>:<version> (by /u/<username>)
const char* USER_AGENT = "esp32:esp-reddit-example:0.1.0 (by /u/yourname)";

reddit::EspTransport transport;
reddit::RedditClient client;

void setup() {
  Serial.begin(115200);
  delay(500);

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" connected");

  // ESP32 has no RTC: sync time BEFORE any HTTPS request or TLS fails.
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  time_t now = 0;
  for (int i = 0; i < 20 && now < 1700000000; i++) {
    delay(500);
    time(&now);
  }
  if (now < 1700000000) {
    Serial.println("NTP sync failed, cannot do TLS");
    return;
  }

  if (!client.beginReadOnly(USER_AGENT, transport)) {
    Serial.printf("beginReadOnly failed: %s\n", client.lastErrorMessage().c_str());
    return;
  }

  reddit::ListingOptions opt;
  opt.limit = 10;
  std::vector<reddit::Submission> posts = client.hot("esp32", opt);
  if (posts.empty() && client.lastError() != reddit::RedditError::Ok) {
    Serial.printf("hot() failed: %s (http %d)\n",
                  client.lastErrorMessage().c_str(), client.lastHttpStatus());
    return;
  }

  Serial.println("--- r/esp32 hot ---");
  for (const auto& p : posts) {
    Serial.printf("[%ld] %s (u/%s, %ld comments)\n",
                  p.score, p.title.c_str(), p.author.c_str(), p.numComments);
  }
}

void loop() {
  // Nothing to do.
}

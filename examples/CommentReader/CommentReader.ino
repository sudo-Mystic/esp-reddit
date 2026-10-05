// CommentReader: read-only demo. Fetches a submission's comment tree and
// prints it indented by depth. No Reddit account needed.

#include <WiFi.h>
#include <time.h>
#include <esp_reddit.h>

const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

// Format: <platform>:<app>:<version> (by /u/<username>)
const char* USER_AGENT = "esp32:esp-reddit-example:0.1.0 (by /u/yourname)";

// The submission to read: r/esp32 post id (bare base36 id from the URL).
const char* SUBREDDIT = "esp32";
const char* POST_ID = "REPLACE_WITH_POST_ID";

reddit::EspTransport transport;
reddit::RedditClient client;

// Recursive print, capped so a huge tree cannot blow the stack.
void printComments(const std::vector<reddit::Comment>& comments, int maxDepth) {
  for (const auto& c : comments) {
    if (c.depth > maxDepth) continue;
    for (int i = 0; i < c.depth; i++) Serial.print("  ");
    Serial.printf("u/%s [%ld]: ", c.author.c_str(), c.score);
    // First line of the body only, truncated for readability.
    std::string first = c.body.substr(0, c.body.find('\n'));
    if (first.size() > 120) first = first.substr(0, 120) + "...";
    Serial.println(first.c_str());
    if (!c.replies.empty()) printComments(c.replies, maxDepth);
  }
}

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
  opt.sort = "top";
  opt.limit = 10;  // top-level comments
  std::vector<reddit::Comment> comments = client.getComments(SUBREDDIT, POST_ID, opt);
  if (comments.empty() && client.lastError() != reddit::RedditError::Ok) {
    Serial.printf("getComments failed: %s (http %d)\n",
                  client.lastErrorMessage().c_str(), client.lastHttpStatus());
    return;
  }

  Serial.printf("--- comments for %s ---\n", POST_ID);
  printComments(comments, 4);
}

void loop() {
  // Nothing to do.
}

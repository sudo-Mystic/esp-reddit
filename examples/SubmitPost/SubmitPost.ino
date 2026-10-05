// SubmitPost: authenticated demo. Submits a text post to a test subreddit.
// NEEDS a Reddit script app: see docs/SETUP.md.
// NEVER commit real credentials. Use placeholders, keep secrets out of git.

#include <WiFi.h>
#include <time.h>
#include <esp_reddit.h>

const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

// --- Reddit script app credentials (placeholders, fill in your own) ---
const char* REDDIT_CLIENT_ID = "YOUR_CLIENT_ID";
const char* REDDIT_CLIENT_SECRET = "YOUR_CLIENT_SECRET";
const char* REDDIT_USERNAME = "YOUR_REDDIT_USERNAME";
const char* REDDIT_PASSWORD = "YOUR_REDDIT_PASSWORD";

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

  if (!client.begin(REDDIT_CLIENT_ID, REDDIT_CLIENT_SECRET,
                    REDDIT_USERNAME, REDDIT_PASSWORD,
                    USER_AGENT, transport)) {
    Serial.printf("begin() failed: %s\n", client.lastErrorMessage().c_str());
    return;
  }
  Serial.println("Authenticated");

  std::string fullname;
  bool ok = client.submitText("test", "Hello from an ESP32",
                              "Posted by a microcontroller running esp-reddit.",
                              &fullname);
  if (ok) {
    Serial.print("Posted: ");
    Serial.println(fullname.c_str());  // t3_xxxxxx
  } else {
    Serial.printf("submitText failed: %s (http %d, reddit code: %s)\n",
                  client.lastErrorMessage().c_str(),
                  client.lastHttpStatus(),
                  client.errorInfo().redditCode.c_str());
  }
}

void loop() {
  // Nothing to do.
}

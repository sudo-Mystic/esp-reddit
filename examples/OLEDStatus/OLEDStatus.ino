// OLEDStatus: read-only demo. Fetches the #1 hot post of r/esp32 and shows
// its title + score on a 128x64 SSD1306 OLED.
//
// Required libraries (add to platformio.ini lib_deps):
//   adafruit/Adafruit SSD1306
//   adafruit/Adafruit GFX Library
// (bblanchon/ArduinoJson is pulled in by esp-reddit automatically.)

#include <WiFi.h>
#include <time.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <esp_reddit.h>

const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

// Format: <platform>:<app>:<version> (by /u/<username>)
const char* USER_AGENT = "esp32:esp-reddit-example:0.1.0 (by /u/yourname)";

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

reddit::EspTransport transport;
reddit::RedditClient client;

void setup() {
  Serial.begin(115200);
  delay(500);

  Wire.begin();

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 not found, check wiring");
    return;
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("r/esp32 top post");
  display.println("connecting...");
  display.display();

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(500);

  // ESP32 has no RTC: sync time BEFORE any HTTPS request or TLS fails.
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  time_t now = 0;
  for (int i = 0; i < 20 && now < 1700000000; i++) {
    delay(500);
    time(&now);
  }
  if (now < 1700000000) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("NTP sync failed");
    display.display();
    return;
  }

  if (!client.beginReadOnly(USER_AGENT, transport)) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("reddit init failed");
    display.display();
    return;
  }

  reddit::ListingOptions opt;
  opt.limit = 1;
  std::vector<reddit::Submission> posts = client.hot("esp32", opt);
  if (posts.empty()) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("no posts / error");
    display.display();
    return;
  }

  const reddit::Submission& top = posts[0];
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("r/esp32 #1 hot:");
  display.println();
  // Title wrapped by the GFX lib; truncate so it fits the 64px screen.
  std::string title = top.title.substr(0, 100);
  display.println(title.c_str());
  display.println();
  display.print("score: ");
  display.println(top.score);
  display.display();

  Serial.printf("[%ld] %s\n", top.score, top.title.c_str());
}

void loop() {
  // Nothing to do.
}

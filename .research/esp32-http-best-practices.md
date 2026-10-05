# ESP32 HTTP Best Practices — Reference for the Reddit wrapper

Research notes for building a lightweight Reddit REST API client on ESP32
(Arduino framework, C++). Verified against ArduinoJson v7 changelogs,
arduino-esp32 core behavior, PlatformIO docs, and Reddit API rules.
Last verified: 2026-10-06.

---

## 1. ArduinoJson v7 on ESP32

### The v6 -> v7 model change (important)

ArduinoJson v7 **removed** `StaticJsonDocument` and `DynamicJsonDocument`.
There is now a single `JsonDocument` whose memory pool is **always heap
allocated** with **elastic capacity** (grows as needed, reuses freed memory).
The old sizing macros (`JSON_ARRAY_SIZE()`, `JSON_OBJECT_SIZE()`,
`JSON_STRING_SIZE()`) are gone, as are `garbageCollect()` and `memoryUsage()`.
`shrinkToFit()` still exists and releases over-allocated memory.

Practical consequence: you no longer pre-size documents for parsing. The
library handles growth, but on ESP32 you still must bound what you parse,
because an unbounded document will happily eat the heap until the TLS stack
or WiFi has nothing left.

### Sizing strategy for large Reddit responses

Reddit listing responses are deeply nested (`data.children[].data.title`,
etc.). A 25-item listing can be 50-150 KB of JSON. Strategy:

1. **Filter, don't parse everything.** ArduinoJson's filter feature parses
   only the fields you whitelist, skipping everything else during
   deserialization. This is the single biggest memory win:

```cpp
JsonDocument filter;
filter["data"]["children"][0]["data"]["title"] = true;
filter["data"]["children"][0]["data"]["author"] = true;
filter["data"]["children"][0]["data"]["score"] = true;
filter["data"]["children"][0]["data"]["id"] = true;

JsonDocument doc;
DeserializationError err = deserializeJson(
    doc, input, DeserializationOption::Filter(filter));
```

   With a filter, a 100 KB listing collapses to a few KB in the document.
   Always filter listing endpoints. Only skip filtering for tiny responses
   (token endpoint, identity).

2. **Stream from the network, don't buffer.** `deserializeJson()` accepts a
   `Stream`, and `HTTPClient::getStream()` returns one. Parse directly:

```cpp
DeserializationError err = deserializeJson(
    doc, http.getStream(), DeserializationOption::Filter(filter));
```

   This avoids holding the full body in a `String` AND the parsed document
   at the same time (double memory). Note: ArduinoJson's stream parser still
   needs the filtered document in RAM, but the raw JSON never sits in a
   buffer.

3. **Cap `limit` on listing endpoints.** Reddit accepts `limit` up to 100.
   For ESP32, request `limit=10` to `limit=25` max. Smaller pages = smaller
   documents = fewer heap surprises. Paginate with `after` if you need more.

4. **Release aggressively.** Call `doc.clear()` (or let the document go out
   of scope) immediately after extracting values into your own structs. Do
   not keep a `JsonDocument` alive across a TLS connection cycle; the
   document's heap blocks compete with mbedTLS's contiguous-buffer needs
   (see section 2).

### Static vs dynamic tradeoffs (v7 reality)

Since the pool is heap-only in v7, the old stack-vs-heap debate is moot.
What matters now:

- **Default allocator** uses `malloc` -> internal SRAM. Fine for filtered
  documents of a few KB.
- **PSRAM option:** for boards with PSRAM, implement a custom allocator by
  subclassing `ArduinoJson::Allocator` and allocating from PSRAM
  (`ps_malloc` / `heap_caps_malloc(MALLOC_CAP_SPIRAM)`), then pass it to the
  `JsonDocument` constructor. This keeps large documents off internal SRAM.
  Caveat from field reports: mbedTLS's own allocator stays on internal RAM
  regardless, so PSRAM only helps the JSON side, not the TLS side.
- **v6 holdouts:** if you pin `bblanchon/ArduinoJson@^6.x` instead, the old
  rules apply: `StaticJsonDocument<N>` on stack (watch stack depth, default
  task stack is ~8 KB), `DynamicJsonDocument` on heap. v7 is recommended;
  v6 is in maintenance mode.

### Typical footprints (ESP32, after WiFi connects: ~150-200 KB free heap)

| Item | Approx. RAM |
|---|---|
| Filtered listing doc (25 posts, title/author/score/id) | 3-8 KB |
| Unfiltered listing doc (25 posts) | 40-120 KB |
| Token response doc | < 1 KB |
| ArduinoJson code | ~15-25 KB flash, negligible RAM |

---

## 2. HTTPClient + WiFiClientSecure patterns

### Client reuse

Create **one** `WiFiClientSecure` and **one** `HTTPClient` as long-lived
objects (globals or members), and re-`begin()` per request. Do not
`new`/`delete` a secure client per request: each construction/destruction
cycle churns the ~7 KB permanent mbedTLS crypto context bookkeeping and
fragments the heap.

```cpp
WiFiClientSecure tlsClient;
HTTPClient http;

void apiGet(const char* url) {
    tlsClient.setCACertBundle(...); // or setCACert(), once at startup
    http.begin(tlsClient, url);
    http.addHeader("User-Agent", USER_AGENT);
    int code = http.GET();
    // ... read stream ...
    http.end(); // closes connection, frees request state; TLS buffers freed on stop()
}
```

`http.end()` must be called after every request or the underlying connection
leaks. `HTTPClient` does not do true keep-alive across `begin()`/`end()`
cycles in practice; expect one TLS handshake per request and budget for it.

### setInsecure() vs proper CA validation

- `tlsClient.setInsecure()` — TLS encryption without certificate verification.
  Immune to MITM only in the "nobody bothers" sense. Acceptable for local
  prototyping; **not** for a published library default.
- `tlsClient.setCACert(pem)` — pin a single CA. Reddit's API is served behind
  a standard public CA chain; embedding one root PEM (~1-2 KB flash) is the
  cheapest correct option and survives as long as Reddit doesn't change CAs.
- `tlsClient.setCACertBundle(data, len)` — validate against the full Mozilla
  root bundle. Heavier (bundle is ~100+ KB flash if embedded), but survives
  CA rotation. Note: on arduino-esp32 3.x the core's built-in bundle symbol
  is not always accessible from `NetworkClientSecure`; the reliable paths
  are a vendored bundle binary (embedded via `board_build.embed_files`) or a
  pinned CA via `setCACert()`.

Recommendation for the wrapper: default to a pinned Reddit CA via
`setCACert()` (small, correct), document `setInsecure()` as a debug escape
hatch behind an explicit flag, never the default.

### TLS handshake memory cost

Per connection, mbedTLS needs (verified from ESP-IDF/arduino-esp32 field data):

| Buffer | Size |
|---|---|
| RX record buffer | ~16 KB (contiguous!) |
| TX record buffer | ~4 KB on ESP32 Arduino (asymmetric content len in recent cores) |
| Handshake context (transient) | ~8-12 KB |
| Certificate chain parsing | ~4-8 KB |
| Global crypto context (permanent, first use) | ~7 KB, never freed |

Total transient: **~30-50 KB of heap per handshake**, with the 16 KB RX
buffer needing to be **contiguous**. On a 520 KB SRAM ESP32 (no PSRAM),
after WiFi (~100 KB) the largest free block is often only ~30-40 KB, so a
handshake can fail with `MBEDTLS_ERR_SSL_ALLOC_FAILED` (-32512) on a
fragmented heap even when total free heap looks fine.

Mitigations:
- Do the first TLS handshake **early**, before other large allocations.
- Never hold a `JsonDocument`, large `String`, or response buffer alive
  across `http.begin()` — parse after the handshake, release before the next
  request.
- Keep the `WiFiClientSecure` object alive (don't destroy/recreate); note
  `stop()` still frees the internal SSL buffers, so the alloc/free cycle per
  request is unavoidable with the Arduino wrapper. Accept it and keep the
  rest of the heap quiet.
- If allocation failures persist, move JSON documents to PSRAM via a custom
  ArduinoJson allocator (section 1).

### Chunked transfer encoding pitfall

ESP32's `HTTPClient` has long-standing bugs decoding `Transfer-Encoding:
chunked` responses (hangs, empty bodies). Reddit serves chunked responses.
The widely used workaround is to force HTTP/1.0, which guarantees
`Content-Length` framing:

```cpp
http.useHTTP10(true);
```

Downside: no keep-alive, but since we handshake per request anyway, nothing
is lost. Apply this by default in the wrapper.

### Timeouts

Set explicit timeouts; the defaults can block for a very long time on a
dead connection:

```cpp
http.setTimeout(15000);        // ms, total request timeout
tlsClient.setHandshakeTimeout(10); // seconds
```

---

## 3. Designing for testability on desktop

### The core pattern: seam the transport

Put **all** ESP32-specific I/O (WiFi, `HTTPClient`, `WiFiClientSecure`,
`configTime`) behind a tiny interface. Everything else — URL building, auth
header construction, JSON parsing into structs, rate-limit math, token
expiry tracking — is pure C++ and must compile on the host.

```cpp
// IHttpClient.h — no Arduino includes, compiles everywhere
#pragma once
#include <string>
#include <map>

struct HttpResponse {
    int statusCode = -1;             // -1 = transport failure
    std::string body;
    std::map<std::string, std::string> headers;
};

class IHttpClient {
public:
    virtual ~IHttpClient() = default;
    virtual HttpResponse get(const std::string& url,
                             const std::map<std::string, std::string>& headers) = 0;
    virtual HttpResponse post(const std::string& url,
                              const std::map<std::string, std::string>& headers,
                              const std::string& body) = 0;
};
```

```cpp
// RedditClient.h — depends only on IHttpClient + ArduinoJson
class RedditClient {
public:
    explicit RedditClient(IHttpClient& http, const Config& cfg);
    bool fetchHot(const std::string& subreddit, int limit, Listing& out);
    // ...
private:
    IHttpClient& http_;
    std::string buildUrl(...);      // pure, unit-testable
    bool parseListing(...);         // pure, unit-testable
};
```

```cpp
// EspHttpClient.h — ESP32 only, implements the seam
#ifdef ARDUINO
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
class EspHttpClient : public IHttpClient {
    HttpResponse get(...) override; // uses HTTPClient internally
};
#endif
```

On the host, tests inject a `FakeHttpClient` that returns canned
`HttpResponse` values. This lets you test parsing, URL building, auth
headers, and rate-limit logic with zero hardware.

### What goes where

| Layer | Host-testable? | Contents |
|---|---|---|
| URL builders, query encoding | Yes | `buildListingUrl(sub, sort, limit, after)` |
| Auth header construction | Yes | `Basic base64(id:secret)`, `Bearer <token>` |
| JSON parsing (with filter) | Yes | `parseListing(json, Listing&)` |
| Rate-limit math | Yes | token bucket, `X-Ratelimit-Reset` handling |
| Token expiry tracking | Yes | `isTokenExpired(now)` with injected clock |
| `EspHttpClient` transport | No (needs hardware or mock at Arduino level) | keep it as thin as possible |
| WiFi/NTP setup helpers | No | thin wrappers, tested on device |

### Arduino API usage in shared code

Shared code will inevitably want `String`, `Serial`, `millis()`. Options:

1. **Prefer `std::string`** in the pure-logic layer. Convert to Arduino
   `String` only at the transport boundary. This is the cleanest for
   testability.
2. **Minimal `Arduino.h` shim** for the native env: a `test/mocks/` header
   providing stub `String`, `Serial`, `millis()`, `delay()`. Works but the
   shim can drift from real Arduino behavior; keep it tiny.
3. ArduinoJson itself compiles fine on host (it's platform-agnostic C++),
   so JSON parsing tests need no shim at all if you use `std::string` inputs.

Recommendation: `std::string` in core logic + ArduinoJson (host-clean),
thin Arduino adapter at the edges.

### Clock injection

Token expiry and rate limiting need "now". Don't call `millis()` or
`time()` directly in logic; inject:

```cpp
using ClockFn = unsigned long (*)(); // or std::function
RedditClient(IHttpClient& http, const Config& cfg, ClockFn now = millis);
```

Tests pass a fake clock; firmware passes `millis`.

---

## 4. PlatformIO specifics

### Library layout (Arduino/PlatformIO compatible)

```
esp-reddit/
  library.json
  platformio.ini          # dev/test project (not shipped in the package)
  src/
    RedditClient.h
    RedditClient.cpp
    IHttpClient.h
    EspHttpClient.h
    EspHttpClient.cpp
  include/                # optional public headers (default header dir)
  test/
    test_url_builder/
      test_main.cpp
    test_parser/
      test_main.cpp
    test_auth/
      test_main.cpp
    test_rate_limit/
      test_main.cpp
  examples/
    basic_usage/
      basic_usage.ino
  .github/
    workflows/
      ci.yml
```

### library.json

```json
{
  "name": "esp-reddit",
  "version": "0.1.0",
  "description": "Lightweight Reddit REST API wrapper for ESP32",
  "keywords": "reddit, api, esp32, rest, oauth",
  "repository": {
    "type": "git",
    "url": "https://github.com/<owner>/esp-reddit.git"
  },
  "authors": [
    { "name": "<name>", "maintainer": true }
  ],
  "license": "MIT",
  "frameworks": "arduino",
  "platforms": "espressif32",
  "dependencies": {
    "bblanchon/ArduinoJson": "^7.0.0"
  },
  "export": {
    "exclude": ["test", ".research", "platformio.ini"]
  }
}
```

Notes:
- `dependencies` uses the short object form: `"owner/name": "version-spec"`.
  SemVer specs: `^7.0.0` (compatible), `~7.0.0` (same minor), exact `7.0.0`.
- `platforms: "espressif32"` + `frameworks: "arduino"` declares compatibility.
  (PIO does not strictly enforce platforms unless `lib_compat_mode = strict`.)
- `export.exclude` keeps tests and dev files out of the published archive;
  verify with `pio pkg pack`.
- Bump `version` on every publish; `pio pkg publish` requires it.

### platformio.ini (dev project with native tests)

```ini
[platformio]
default_envs = esp32dev

[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
lib_deps =
  bblanchon/ArduinoJson@^7.0.0
build_flags =
  -Wall
  -DCORE_DEBUG_LEVEL=3

[env:native]
platform = native
test_framework = unity
test_build_src = true
build_flags =
  -std=c++17
  -Wall
  -g
build_src_filter =
  +<*>
  -<EspHttpClient.cpp>   ; ESP32-only transport excluded from host build
lib_deps =
  bblanchon/ArduinoJson@^7.0.0
```

Key points:
- `platform = native` builds with host g++/clang; no board needed.
- `test_framework = unity` pulls Unity; tests run via `pio test -e native`.
- `test_build_src = true` compiles `src/` into the test binary so tests can
  `#include` library headers directly.
- `build_src_filter` excludes ESP32-only `.cpp` files from the host build.
  Guard ESP32-only headers with `#ifdef ARDUINO` as well (belt and braces).
- `lib_deps` under `[env:native]` gives tests ArduinoJson on the host.

### Test layout (Unity)

Each suite is a directory `test/test_<name>/` containing one `.cpp` file
(PIO requires the file be named `test_*.cpp` or `*_test.cpp`; the
conventional name is `test_main.cpp`):

```cpp
// test/test_parser/test_main.cpp
#include <unity.h>
#include "RedditClient.h"   // via test_build_src
#include "FakeHttpClient.h" // test-local helper

void setUp() {}
void tearDown() {}

void test_parse_hot_listing_extracts_titles() {
    FakeHttpClient fake;
    fake.queueResponse(200, SAMPLE_LISTING_JSON);
    RedditClient client(fake, testConfig());
    Listing out;
    TEST_ASSERT_TRUE(client.fetchHot("esp32", 5, out));
    TEST_ASSERT_EQUAL_size_t(5, out.posts.size());
    TEST_ASSERT_EQUAL_STRING("...", out.posts[0].title.c_str());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_parse_hot_listing_extracts_titles);
    return UNITY_END();
}
```

Run: `pio test -e native` (all suites) or `pio test -e native -f test_parser`
(one suite), `-v` for verbose Unity output. `pio test` exits non-zero on
failure, which is what CI gates on.

### CI (.github/workflows/ci.yml)

```yaml
name: CI
on:
  push:
    branches: [main]
  pull_request:
    branches: [main]
permissions:
  contents: read
jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-python@v5
        with:
          python-version: "3.11"
      - uses: actions/cache@v4
        with:
          path: |
            ~/.cache/pip
            ~/.platformio/.cache
          key: ${{ runner.os }}-pio-${{ hashFiles('platformio.ini') }}
      - name: Install PlatformIO
        run: pip install --upgrade platformio
      - name: Native unit tests
        run: pio test -e native
  build:
    runs-on: ubuntu-latest
    needs: test
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-python@v5
        with:
          python-version: "3.11"
      - uses: actions/cache@v4
        with:
          path: |
            ~/.cache/pip
            ~/.platformio/.cache
          key: ${{ runner.os }}-pio-${{ hashFiles('platformio.ini') }}
      - name: Install PlatformIO
        run: pip install --upgrade platformio
      - name: Build for ESP32
        run: pio run -e esp32dev
```

This gates every push/PR on host unit tests, then proves the firmware still
compiles for the real target. No hardware needed.

---

## 5. Memory-safety rules for the wrapper

1. **Cap everything with named constants.**
   `MAX_LISTING_LIMIT = 25`, `MAX_TITLE_LEN = 300`, `MAX_BODY_LEN = 4000`
   (Reddit's own comment limit is 10k; truncate client-side), response body
   cap for non-streamed reads. Never let a server-controlled size drive an
   allocation without a clamp.

2. **Avoid `String` fragmentation.**
   - Call `str.reserve(n)` before repeated `+=` concatenation.
   - Prefer fixed `char buf[N]` + `snprintf` for URL/header building; the
     sizes are knowable (URLs < 512 chars, headers < 256).
   - Never `String body = http.getString()` on a listing endpoint; stream
     parse instead (section 1). `getString()` duplicates the entire body on
     the heap next to the TLS buffers.
   - If you must accumulate, accumulate into a bounded buffer and abort
     cleanly past the cap.

3. **One big thing at a time.** The heap budget during a request is roughly:
   TLS (~40 KB transient) + JSON doc (filtered, few KB) + your structs.
   Sequence operations so these don't overlap: handshake -> stream-parse ->
   `http.end()` -> release doc -> process. Never parse JSON while a second
   TLS connection is open.

4. **Feed the watchdog.** TLS handshakes and large parses can take seconds.
   The ESP32 task watchdog defaults to ~5 s; the IDLE watchdog will also
   bite if you starve it. Call `yield()` (or a short `delay(1)`) inside any
   loop that processes response data, and after `http.GET()` returns before
   a long parse. Longest risk window is the handshake itself, which you
   can't yield inside; keep total heap calm so it finishes fast.

5. **Check allocation results.** `deserializeJson` returns
   `DeserializationError`; `NoMemory` is a normal, expected outcome on
   ESP32, not a crash. Map it to a clean `Result::OUT_OF_MEMORY` and let the
   caller retry with a smaller `limit`. Same for `http.GET()` returning
   negative (transport error) — never treat as HTTP status.

6. **Log heap in debug builds.** `ESP.getFreeHeap()` /
   `ESP.getMaxAllocHeap()` (largest contiguous block — the number that
   actually matters for the 16 KB TLS buffer) around requests in a debug
   mode. The sum lies; the largest block tells the truth.

---

## 6. Known pitfalls

### Reddit: descriptive User-Agent is mandatory

Reddit rate-limits and blocks clients without a proper User-Agent. The
required format is:

```
<platform>:<app ID>:<version> (by /u/<reddit username>)
```

Example: `esp32:esp-reddit:v0.1.0 (by /u/yourname)`. Missing or generic
agents (e.g. `ESP32HTTPClient`, `python-requests/...`) get 429s or blocks.
Set it on **every** request, including the token request. Never spoof a
browser UA.

### NTP time is required before TLS

ESP32 has no RTC; on boot the clock reads 1970. Certificate validation
checks notBefore/notAfter, so every cert looks invalid and the handshake
fails with an opaque `-1`. Sync time **after WiFi connects, before the first
HTTPS request**:

```cpp
configTime(0, 0, "pool.ntp.org", "time.nist.gov");
// wait until time is sane
time_t now = 0;
for (int i = 0; i < 20 && now < 1700000000; i++) {
    delay(500);
    time(&now);
}
```

If NTP fails, fail loudly at startup rather than producing mysterious TLS
errors later. Re-sync periodically; drift over days can invalidate certs.

### OAuth specifics that bite on embedded

- Token endpoint: `POST https://www.reddit.com/api/v1/access_token` with
  HTTP Basic auth `base64(client_id:client_secret)`, body
  `grant_type=password&username=...&password=...`
  (`application/x-www-form-urlencoded`).
- Access tokens live **3600 s**. Track `expires_at = now + expires_in - 60`
  (60 s safety margin) and re-auth proactively. Script-type apps get **no
  refresh token**; just re-run the password grant.
- Authenticated API base is `https://oauth.reddit.com` (not `www.reddit.com`).
- Rate limit: **60 req/min per OAuth token**; watch `X-Ratelimit-Remaining`
  / `X-Ratelimit-Reset` response headers and back off on 429. Anonymous
  `.json` endpoints are ~10 req/min — always use OAuth.
- URL-encode form bodies and query params (`%20`, not `+`, for spaces in
  most Reddit endpoints; form-encode per `application/x-www-form-urlencoded`
  for POST bodies).

### DNS

`WiFiClientSecure` resolves via lwIP DNS; on captive portals or broken DHCP
DNS, `connect()` fails before TLS even starts. The failure looks identical
to a TLS failure (`http.GET()` returns -1). When diagnosing, resolve first
with `WiFi.hostByName("oauth.reddit.com", ip)` and log the result to
separate DNS failures from TLS failures.

### Error code discipline

`HTTPClient::GET()` returns **negative** values for transport errors and
**positive** HTTP status codes. A common bug is treating `-1` as an HTTP
status. The wrapper's result type must distinguish at least:
`OK`, `TRANSPORT_ERROR`, `HTTP_ERROR(status)`, `PARSE_ERROR`,
`OUT_OF_MEMORY`, `RATE_LIMITED`, `AUTH_FAILED`. Callers (and tests) depend
on this taxonomy.

---

## Quick decision log (for implementers)

| Decision | Choice | Why |
|---|---|---|
| ArduinoJson version | v7 (`^7.0.0`) | Current, elastic docs, maintained |
| JSON parsing | Stream + `DeserializationOption::Filter` | 10-30x memory reduction on listings |
| TLS trust | Pinned CA via `setCACert()`, `setInsecure()` opt-in only | Small flash cost, correct default |
| HTTP version | `useHTTP10(true)` | Avoids HTTPClient chunked-decoding bugs |
| Transport seam | `IHttpClient` virtual interface | Host unit tests with fake transport |
| Core string type | `std::string` in logic, Arduino `String` at edges | Compiles on host without shims |
| Test framework | Unity via `pio test -e native` | Zero hardware, CI-gatable |
| Listing page size | Default 10, max 25 | Heap safety on 520 KB SRAM parts |
| Time | `configTime` at startup, injected clock in logic | TLS needs it; tests need determinism |

---

## Sources

- ArduinoJson v7 changelogs (7.0.0 / 7.3.0), ESP Component Registry —
  `StaticJsonDocument`/`DynamicJsonDocument` merged into elastic `JsonDocument`
- arduino-esp32 issues/PRs on `WiFiClientSecure` memory, `setCACertBundle`
  availability, and TLS write chunking
- ESP-IDF mbedTLS defaults: 16 KB RX / 4 KB TX record buffers on ESP32 Arduino
- PlatformIO docs: `library.json` manifest, `platform = native`, Unity
  testing, GitHub Actions integration
- Reddit API rules: User-Agent format, 60 req/min OAuth limit,
  `X-Ratelimit-*` headers, script-app token behavior (no refresh tokens)

#pragma once
// Shared test helpers: fake transport + fake clock. Included by the
// native Unity test suites via a relative path.
#include <string>
#include <vector>
#include <queue>
#include "RedditTransport.h"
#include "RedditTime.h"

namespace reddittest {

class FakeTransport : public reddit::ITransport {
 public:
  struct Expectation {
    std::string method;
    std::string urlContains;  // substring the URL must contain ("" = any)
    reddit::HttpResponse response;
  };

  void queue(const std::string& method, const std::string& urlContains,
             reddit::HttpResponse resp) {
    _queue.push(Expectation{method, urlContains, std::move(resp)});
  }
  void queueGet(const std::string& urlContains, int status,
                const std::string& body) {
    reddit::HttpResponse r;
    r.status = status;
    r.body = body;
    queue("GET", urlContains, std::move(r));
  }
  void queuePost(const std::string& urlContains, int status,
                 const std::string& body) {
    reddit::HttpResponse r;
    r.status = status;
    r.body = body;
    queue("POST", urlContains, std::move(r));
  }

  reddit::HttpResponse send(const reddit::HttpRequest& req) override {
    lastRequest = req;
    ++calls;
    if (_queue.empty()) {
      reddit::HttpResponse r;
      r.status = 500;
      r.body = "fake transport: no queued response";
      return r;
    }
    Expectation e = std::move(_queue.front());
    _queue.pop();
    if (!e.method.empty() && e.method != req.method) {
      reddit::HttpResponse r;
      r.status = 500;
      r.body = "fake transport: method mismatch";
      return r;
    }
    if (!e.urlContains.empty() &&
        req.url.find(e.urlContains) == std::string::npos) {
      reddit::HttpResponse r;
      r.status = 500;
      r.body = "fake transport: url mismatch: " + req.url;
      return r;
    }
    return e.response;
  }

  std::string header(const std::string& name) const {
    for (const auto& h : lastRequest.headers)
      if (h.name == name) return h.value;
    return "";
  }

  reddit::HttpRequest lastRequest;
  int calls = 0;

 private:
  std::queue<Expectation> _queue;
};

// Fake clock for reddit::nowMs(). Advance manually.
struct FakeClock {
  static uint32_t t;
  static uint32_t now() { return t; }
  static void install() { reddit::setClock(now); t = 0; }
  static void uninstall() { reddit::setClock(nullptr); }
  static void advance(uint32_t ms) { t += ms; }
};
inline uint32_t FakeClock::t = 0;

}  // namespace reddittest

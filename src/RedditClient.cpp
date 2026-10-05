#include "RedditClient.h"

#include <ArduinoJson.h>

#include <cstdlib>  // strtol
#include <string>
#include <vector>

#include "RedditTime.h"
#include "RedditUtil.h"

#ifdef ARDUINO
#include <Arduino.h>  // ::delay()
#else
#include <chrono>
#include <thread>
#endif

namespace reddit {

// ---------- file-local helpers ----------

static void setErr(ErrorInfo& err, RedditError code, const std::string& msg,
                   int httpStatus = 0) {
  err.code = code;
  err.message = msg;
  err.redditCode.clear();
  err.httpStatus = httpStatus;
}

// Write endpoints need a real login; read-only mode has no identity.
static bool requireWrite(bool readOnly, ErrorInfo& err, const char* what) {
  if (!readOnly) return true;
  setErr(err, RedditError::InvalidArgs,
         std::string(what) + ": requires authentication (read-only mode)");
  return false;
}

static long parseLong(const std::string& s) {
  if (s.empty()) return 0;
  char* end = nullptr;
  long v = std::strtol(s.c_str(), &end, 10);
  return (end == s.c_str()) ? 0 : v;
}

static bool endsWith(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Be liberal with subreddit input: "esp32", "r/esp32" and "/r/esp32" all work.
// The name itself is passed through as given (Reddit paths are
// case-insensitive); empty after normalization means invalid input.
static std::string normalizeSubreddit(const std::string& in) {
  std::string s = in;
  while (!s.empty() && s.front() == '/') s.erase(s.begin());
  if (s.size() > 2 && (s[0] == 'r' || s[0] == 'R') && s[1] == '/') {
    s.erase(0, 2);
  }
  return s;
}

static int clampLimit(const ListingOptions& opt, const RedditConfig& cfg) {
  int limit = opt.limit <= 0 ? static_cast<int>(cfg.defaultLimit) : opt.limit;
  if (limit > static_cast<int>(cfg.maxLimit)) limit = cfg.maxLimit;
  if (limit > 100) limit = 100;  // Reddit hard cap
  if (limit < 1) limit = 1;
  return limit;
}

// Standard listing query string. `timed` adds t= for /top and /controversial.
static std::string listingQuery(const ListingOptions& opt, int limit,
                                bool timed) {
  std::string q = "?raw_json=1&limit=" + std::to_string(limit);
  if (!opt.after.empty()) q += "&after=" + util::urlEncode(opt.after);
  if (!opt.before.empty()) q += "&before=" + util::urlEncode(opt.before);
  if (timed && !opt.time.empty()) q += "&t=" + util::urlEncode(opt.time);
  return q;
}

static HttpRequest buildHttpRequest(const std::string& method,
                                    const std::string& url,
                                    const std::string& userAgent,
                                    const std::string& bearer,
                                    const std::string& body,
                                    uint32_t timeoutMs) {
  HttpRequest req;
  req.method = method;
  req.url = url;
  req.timeoutMs = timeoutMs;
  req.headers.push_back(HttpHeader{"User-Agent", userAgent});
  if (!bearer.empty()) {
    req.headers.push_back(HttpHeader{"Authorization", bearer});
  }
  if (method == "POST") {
    req.headers.push_back(
        HttpHeader{"Content-Type", "application/x-www-form-urlencoded"});
  }
  req.body = body;
  return req;
}

// Map a mutation response body onto the error state. Several write endpoints
// (vote/save/unsave/del/subscribe) answer HTTP 200 with an empty body on
// success, so "nothing to parse" is success too.
static bool handleMutationResult(const std::string& body, ErrorInfo& err,
                                 std::string* outFullname) {
  if (body.find_first_not_of(" \t\r\n") == std::string::npos) {
    setErr(err, RedditError::Ok, "");
    return true;
  }
  std::string code, msg, newId;
  if (parseMutationResult(body, code, msg, newId)) {
    setErr(err, RedditError::Ok, "");
    if (outFullname) *outFullname = newId;
    return true;
  }
  if (code == "RATELIMIT") {
    setErr(err, RedditError::SpamBlocked,
           msg.empty() ? "RATELIMIT: slow down" : msg);
  } else {
    setErr(err, RedditError::ApiError,
           msg.empty() ? ("Reddit API error: " + code) : msg);
  }
  err.redditCode = code;
  return false;
}

// Flat parse of a /user/{name}/comments Listing (children are t1). Replies
// are intentionally not expanded here; use getComments() for trees.
static bool parseUserCommentsListing(const std::string& json,
                                     std::vector<Comment>& out) {
  out.clear();
  JsonDocument doc;
  if (deserializeJson(doc, json)) return false;
  JsonVariant data = doc["data"];
  if (!data.is<JsonObject>()) return false;
  JsonVariant children = data["children"];
  if (!children.is<JsonArray>()) return false;
  for (JsonVariant child : children.as<JsonArray>()) {
    if (!child.is<JsonObject>()) continue;
    if (std::string(child["kind"] | "") != "t1") continue;  // skip "more"
    JsonVariant d = child["data"];
    if (!d.is<JsonObject>()) continue;
    Comment c;
    c.id = std::string(d["id"] | "");
    c.fullname = std::string(d["name"] | "");
    c.body = std::string(d["body"] | "");
    c.author = std::string(d["author"] | "");
    c.subreddit = std::string(d["subreddit"] | "");
    c.linkId = std::string(d["link_id"] | "");
    c.parentId = std::string(d["parent_id"] | "");
    c.score = d["score"] | 0L;
    c.createdUtc = d["created_utc"] | 0.0;
    c.depth = d["depth"] | 0;
    c.scoreHidden = d["score_hidden"] | false;
    c.stickied = d["stickied"] | false;
    out.push_back(c);
  }
  return true;
}

// ---------- lifecycle ----------

bool RedditClient::begin(const std::string& clientId,
                         const std::string& clientSecret,
                         const std::string& username,
                         const std::string& password,
                         const std::string& userAgent, ITransport& transport,
                         const RedditConfig& config) {
  if (clientId.empty() || clientSecret.empty() || username.empty() ||
      password.empty() || userAgent.empty()) {
    setErr(_err, RedditError::InvalidArgs,
           "begin: clientId, clientSecret, username, password and userAgent "
           "must all be non-empty");
    return false;
  }
  if (!_auth.configure(clientId, clientSecret, username, password,
                       userAgent)) {
    setErr(_err, RedditError::InvalidArgs, "begin: auth configuration failed");
    return false;
  }
  _transport = &transport;
  _config = config;
  _userAgent = userAgent;
  _readOnly = false;
  _lastAfter.clear();
  _lastRequestMs = 0;
  _backoffUntilMs = 0;
  _lastRemaining = -1;
  _err = ErrorInfo();
  return true;
}

bool RedditClient::beginReadOnly(const std::string& userAgent,
                                 ITransport& transport,
                                 const RedditConfig& config) {
  if (userAgent.empty()) {
    setErr(_err, RedditError::InvalidArgs,
           "beginReadOnly: userAgent must not be empty");
    return false;
  }
  _transport = &transport;
  _config = config;
  _userAgent = userAgent;
  _readOnly = true;
  _lastAfter.clear();
  _lastRequestMs = 0;
  _backoffUntilMs = 0;
  _lastRemaining = -1;
  _err = ErrorInfo();
  return true;
}

bool RedditClient::isAuthenticated() const {
  return _transport != nullptr && !_readOnly && _auth.isConfigured();
}

std::string RedditClient::baseUrl() const {
  return _readOnly ? "https://www.reddit.com" : "https://oauth.reddit.com";
}

// ---------- low-level plumbing ----------

bool RedditClient::requireAuth() {
  if (!_transport) {
    setErr(_err, RedditError::NotInitialized, "begin() was not called");
    return false;
  }
  if (!_readOnly && !_auth.isConfigured()) {
    setErr(_err, RedditError::NotInitialized,
           "auth not configured (begin() failed or was never called)");
    return false;
  }
  return true;
}

bool RedditClient::pace() {
  uint32_t now = nowMs();
  // Server-driven backoff in effect: fail fast, never sleep through it.
  if (_backoffUntilMs != 0) {
    if (static_cast<int32_t>(_backoffUntilMs - now) > 0) {
      setErr(_err, RedditError::RateLimited,
             "rate-limit backoff active; try again later");
      return false;
    }
    _backoffUntilMs = 0;  // backoff window has passed
  }
  uint32_t interval = _config.minRequestIntervalMs;
  if (_lastRequestMs != 0 && interval > 0) {
    uint32_t elapsed = now - _lastRequestMs;  // unsigned: wrap-safe
    if (elapsed < interval) {
      uint32_t waitMs = interval - elapsed;
#ifdef ARDUINO
      ::delay(waitMs);
#else
      std::this_thread::sleep_for(std::chrono::milliseconds(waitMs));
#endif
    }
  }
  _lastRequestMs = nowMs();
  return true;
}

bool RedditClient::checkHttp(const HttpResponse& r) {
  // Track the live rate-limit budget from the response headers.
  if (!r.rateLimitRemaining.empty()) {
    _lastRemaining = static_cast<int>(parseLong(r.rateLimitRemaining));
  }
  if (!r.rateLimitReset.empty() && _lastRemaining >= 0 &&
      _lastRemaining <= _config.rateLimitFloor) {
    long resetSec = parseLong(r.rateLimitReset);
    if (resetSec > 0) {
      // Proactive pause: budget nearly exhausted, wait for the reset.
      _backoffUntilMs = nowMs() + static_cast<uint32_t>(resetSec * 1000L);
    }
  }

  if (r.status <= 0) {
    setErr(_err,
           r.timeout ? RedditError::HttpTimeout : RedditError::TransportError,
           r.timeout ? "HTTP request timed out"
                     : "transport failure (no HTTP response)");
    return false;
  }
  if (r.status >= 200 && r.status < 300) {
    setErr(_err, RedditError::Ok, "", r.status);
    return true;
  }
  switch (r.status) {
    case 401:
      // Bad/expired token. Drop it so the retry path below re-authenticates
      // instead of replaying a dead token.
      _auth.invalidate();
      setErr(_err, RedditError::Unauthorized,
             "HTTP 401: bad or expired token", r.status);
      break;
    case 403:
      // Valid token, forbidden action: never re-auth on 403.
      setErr(_err, RedditError::Forbidden,
             "HTTP 403: action forbidden for this account/token", r.status);
      break;
    case 404:
      setErr(_err, RedditError::NotFound, "HTTP 404: not found", r.status);
      break;
    case 429: {
      long retryAfter = parseLong(r.retryAfter);
      long waitSec = retryAfter > 60 ? retryAfter : 60;
      _backoffUntilMs = nowMs() + static_cast<uint32_t>(waitSec * 1000L);
      setErr(_err, RedditError::RateLimited, "HTTP 429: rate limited",
             r.status);
      break;
    }
    default:
      setErr(_err, RedditError::HttpError,
             "HTTP " + std::to_string(r.status), r.status);
      break;
  }
  return false;
}

HttpResponse RedditClient::apiGet(const std::string& path) {
  HttpResponse r;
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (!requireAuth()) return HttpResponse{};
    if (!pace()) return HttpResponse{};
    std::string bearer;
    if (!_readOnly) {
      if (!_auth.ensureToken(*_transport, _config.tokenRefreshMarginSec)) {
        RedditError ae = _auth.lastError();
        if (ae == RedditError::Ok) ae = RedditError::TokenRefreshFailed;
        const std::string& am = _auth.lastErrorMessage();
        setErr(_err, ae,
               am.empty() ? "failed to obtain OAuth token" : am);
        return HttpResponse{};
      }
      bearer = _auth.authorizationHeader();
    }
    HttpRequest req = buildHttpRequest("GET", baseUrl() + path, _userAgent,
                                       bearer, "", _config.httpTimeoutMs);
    r = _transport->send(req);
    if (checkHttp(r)) return r;
    // 401 in auth mode: checkHttp() invalidated the cached token, so retry
    // exactly once — ensureToken() above will fetch a fresh one.
    if (_err.code == RedditError::Unauthorized && !_readOnly && attempt == 0) {
      continue;
    }
    return r;
  }
  return r;
}

HttpResponse RedditClient::apiPost(const std::string& path,
                                   const std::string& formBody) {
  HttpResponse r;
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (!requireAuth()) return HttpResponse{};
    if (!pace()) return HttpResponse{};
    std::string bearer;
    if (!_readOnly) {
      if (!_auth.ensureToken(*_transport, _config.tokenRefreshMarginSec)) {
        RedditError ae = _auth.lastError();
        if (ae == RedditError::Ok) ae = RedditError::TokenRefreshFailed;
        const std::string& am = _auth.lastErrorMessage();
        setErr(_err, ae,
               am.empty() ? "failed to obtain OAuth token" : am);
        return HttpResponse{};
      }
      bearer = _auth.authorizationHeader();
    }
    HttpRequest req = buildHttpRequest("POST", baseUrl() + path, _userAgent,
                                       bearer, formBody, _config.httpTimeoutMs);
    r = _transport->send(req);
    if (checkHttp(r)) return r;
    // 401 in auth mode: checkHttp() invalidated the cached token, so retry
    // exactly once — ensureToken() above will fetch a fresh one.
    if (_err.code == RedditError::Unauthorized && !_readOnly && attempt == 0) {
      continue;
    }
    return r;
  }
  return r;
}

// ---------- listings ----------

std::vector<Submission> RedditClient::listing(const std::string& path,
                                              const ListingOptions& opt) {
  int limit = clampLimit(opt, _config);
  bool timed = endsWith(path, "/top") || endsWith(path, "/controversial");
  HttpResponse r = apiGet(path + listingQuery(opt, limit, timed));
  if (_err.code != RedditError::Ok) return {};
  std::vector<Submission> out;
  std::string after;
  if (!parseSubmissionListing(r.body, out, after,
                              static_cast<uint8_t>(limit))) {
    setErr(_err, RedditError::JsonError, "failed to parse listing JSON");
    return {};
  }
  _lastAfter = after;
  return out;
}

std::vector<Submission> RedditClient::hot(const std::string& subreddit,
                                          const ListingOptions& opt) {
  std::string sub = normalizeSubreddit(subreddit);
  if (sub.empty()) {
    setErr(_err, RedditError::InvalidArgs, "hot: subreddit must not be empty");
    return {};
  }
  return listing("/r/" + util::urlEncode(sub) + "/hot", opt);
}

std::vector<Submission> RedditClient::newPosts(const std::string& subreddit,
                                               const ListingOptions& opt) {
  std::string sub = normalizeSubreddit(subreddit);
  if (sub.empty()) {
    setErr(_err, RedditError::InvalidArgs,
           "newPosts: subreddit must not be empty");
    return {};
  }
  return listing("/r/" + util::urlEncode(sub) + "/new", opt);
}

std::vector<Submission> RedditClient::rising(const std::string& subreddit,
                                             const ListingOptions& opt) {
  std::string sub = normalizeSubreddit(subreddit);
  if (sub.empty()) {
    setErr(_err, RedditError::InvalidArgs,
           "rising: subreddit must not be empty");
    return {};
  }
  return listing("/r/" + util::urlEncode(sub) + "/rising", opt);
}

std::vector<Submission> RedditClient::controversial(
    const std::string& subreddit, const ListingOptions& opt) {
  std::string sub = normalizeSubreddit(subreddit);
  if (sub.empty()) {
    setErr(_err, RedditError::InvalidArgs,
           "controversial: subreddit must not be empty");
    return {};
  }
  return listing("/r/" + util::urlEncode(sub) + "/controversial", opt);
}

std::vector<Submission> RedditClient::top(const std::string& subreddit,
                                          const ListingOptions& opt) {
  std::string sub = normalizeSubreddit(subreddit);
  if (sub.empty()) {
    setErr(_err, RedditError::InvalidArgs, "top: subreddit must not be empty");
    return {};
  }
  return listing("/r/" + util::urlEncode(sub) + "/top", opt);
}

// ---------- submission + comments ----------

bool RedditClient::getSubmission(const std::string& subreddit,
                                 const std::string& id36, Submission& out) {
  std::string sub = normalizeSubreddit(subreddit);
  if (sub.empty() || id36.empty()) {
    setErr(_err, RedditError::InvalidArgs,
           "getSubmission: subreddit and id36 must not be empty");
    return false;
  }
  HttpResponse r = apiGet("/r/" + util::urlEncode(sub) + "/comments/" +
                          util::urlEncode(id36) + "?raw_json=1&limit=1");
  if (_err.code != RedditError::Ok) return false;
  std::vector<Comment> comments;
  if (!parseCommentsResponse(r.body, out, comments, _config.maxCommentDepth)) {
    setErr(_err, RedditError::JsonError,
           "getSubmission: failed to parse comments JSON");
    return false;
  }
  return true;
}

std::vector<Comment> RedditClient::getComments(const std::string& subreddit,
                                              const std::string& id36,
                                              const ListingOptions& opt) {
  std::string sub = normalizeSubreddit(subreddit);
  if (sub.empty() || id36.empty()) {
    setErr(_err, RedditError::InvalidArgs,
           "getComments: subreddit and id36 must not be empty");
    return {};
  }
  std::string q = "?raw_json=1";
  if (!opt.sort.empty()) q += "&sort=" + util::urlEncode(opt.sort);
  if (opt.depth > 0) q += "&depth=" + std::to_string(opt.depth);
  if (opt.limit > 0) {
    int lim = opt.limit > 100 ? 100 : opt.limit;
    q += "&limit=" + std::to_string(lim);
  }
  HttpResponse r = apiGet("/r/" + util::urlEncode(sub) + "/comments/" +
                          util::urlEncode(id36) + q);
  if (_err.code != RedditError::Ok) return {};
  Submission post;
  std::vector<Comment> comments;
  if (!parseCommentsResponse(r.body, post, comments, _config.maxCommentDepth)) {
    setErr(_err, RedditError::JsonError,
           "getComments: failed to parse comments JSON");
    return {};
  }
  return comments;
}

// ---------- mutations ----------

bool RedditClient::submitText(const std::string& subreddit,
                              const std::string& title,
                              const std::string& text,
                              std::string* outFullname) {
  if (!requireWrite(_readOnly, _err, "submitText")) return false;
  std::string sub = normalizeSubreddit(subreddit);
  if (sub.empty() || title.empty()) {
    setErr(_err, RedditError::InvalidArgs,
           "submitText: subreddit and title must not be empty");
    return false;
  }
  std::string t = title.size() > 300 ? title.substr(0, 300) : title;
  std::string body = util::buildFormBody({{"api_type", "json"},
                                          {"sr", sub},
                                          {"kind", "self"},
                                          {"title", t},
                                          {"text", text},
                                          {"resubmit", "true"},
                                          {"sendreplies", "true"}});
  HttpResponse r = apiPost("/api/submit", body);
  if (_err.code != RedditError::Ok) return false;
  return handleMutationResult(r.body, _err, outFullname);
}

bool RedditClient::submitLink(const std::string& subreddit,
                              const std::string& title, const std::string& url,
                              std::string* outFullname) {
  if (!requireWrite(_readOnly, _err, "submitLink")) return false;
  std::string sub = normalizeSubreddit(subreddit);
  if (sub.empty() || title.empty() || url.empty()) {
    setErr(_err, RedditError::InvalidArgs,
           "submitLink: subreddit, title and url must not be empty");
    return false;
  }
  std::string t = title.size() > 300 ? title.substr(0, 300) : title;
  std::string body = util::buildFormBody({{"api_type", "json"},
                                          {"sr", sub},
                                          {"kind", "link"},
                                          {"title", t},
                                          {"url", url},
                                          {"resubmit", "true"},
                                          {"sendreplies", "true"}});
  HttpResponse r = apiPost("/api/submit", body);
  if (_err.code != RedditError::Ok) return false;
  return handleMutationResult(r.body, _err, outFullname);
}

bool RedditClient::vote(const std::string& fullname, int dir) {
  if (!requireWrite(_readOnly, _err, "vote")) return false;
  if (fullname.empty() || (dir != 1 && dir != 0 && dir != -1)) {
    setErr(_err, RedditError::InvalidArgs,
           "vote: fullname must not be empty and dir must be -1, 0 or 1");
    return false;
  }
  std::string body = util::buildFormBody({{"api_type", "json"},
                                          {"id", fullname},
                                          {"dir", std::to_string(dir)}});
  HttpResponse r = apiPost("/api/vote", body);
  if (_err.code != RedditError::Ok) return false;
  return handleMutationResult(r.body, _err, nullptr);
}

bool RedditClient::save(const std::string& fullname) {
  if (!requireWrite(_readOnly, _err, "save")) return false;
  if (fullname.empty()) {
    setErr(_err, RedditError::InvalidArgs, "save: fullname must not be empty");
    return false;
  }
  std::string body =
      util::buildFormBody({{"api_type", "json"}, {"id", fullname}});
  HttpResponse r = apiPost("/api/save", body);
  if (_err.code != RedditError::Ok) return false;
  return handleMutationResult(r.body, _err, nullptr);
}

bool RedditClient::unsave(const std::string& fullname) {
  if (!requireWrite(_readOnly, _err, "unsave")) return false;
  if (fullname.empty()) {
    setErr(_err, RedditError::InvalidArgs,
           "unsave: fullname must not be empty");
    return false;
  }
  std::string body =
      util::buildFormBody({{"api_type", "json"}, {"id", fullname}});
  HttpResponse r = apiPost("/api/unsave", body);
  if (_err.code != RedditError::Ok) return false;
  return handleMutationResult(r.body, _err, nullptr);
}

bool RedditClient::del(const std::string& fullname) {
  if (!requireWrite(_readOnly, _err, "del")) return false;
  if (fullname.empty()) {
    setErr(_err, RedditError::InvalidArgs, "del: fullname must not be empty");
    return false;
  }
  std::string body =
      util::buildFormBody({{"api_type", "json"}, {"id", fullname}});
  HttpResponse r = apiPost("/api/del", body);
  if (_err.code != RedditError::Ok) return false;
  return handleMutationResult(r.body, _err, nullptr);
}

bool RedditClient::editText(const std::string& fullname,
                            const std::string& text) {
  if (!requireWrite(_readOnly, _err, "editText")) return false;
  if (fullname.empty() || text.empty()) {
    setErr(_err, RedditError::InvalidArgs,
           "editText: fullname and text must not be empty");
    return false;
  }
  std::string body = util::buildFormBody(
      {{"api_type", "json"}, {"thing_id", fullname}, {"text", text}});
  HttpResponse r = apiPost("/api/editusertext", body);
  if (_err.code != RedditError::Ok) return false;
  return handleMutationResult(r.body, _err, nullptr);
}

bool RedditClient::comment(const std::string& parentFullname,
                           const std::string& text, std::string* outFullname) {
  if (!requireWrite(_readOnly, _err, "comment")) return false;
  if (parentFullname.empty() || text.empty()) {
    setErr(_err, RedditError::InvalidArgs,
           "comment: parentFullname and text must not be empty");
    return false;
  }
  std::string body = util::buildFormBody(
      {{"api_type", "json"}, {"thing_id", parentFullname}, {"text", text}});
  HttpResponse r = apiPost("/api/comment", body);
  if (_err.code != RedditError::Ok) return false;
  return handleMutationResult(r.body, _err, outFullname);
}

bool RedditClient::subscribe(const std::string& subreddit, bool doSubscribe) {
  if (!requireWrite(_readOnly, _err, "subscribe")) return false;
  std::string sub = normalizeSubreddit(subreddit);
  if (sub.empty()) {
    setErr(_err, RedditError::InvalidArgs,
           "subscribe: subreddit must not be empty");
    return false;
  }
  std::string body =
      util::buildFormBody({{"api_type", "json"},
                           {"action", doSubscribe ? "sub" : "unsub"},
                           {"sr_name", sub}});
  HttpResponse r = apiPost("/api/subscribe", body);
  if (_err.code != RedditError::Ok) return false;
  return handleMutationResult(r.body, _err, nullptr);
}

// ---------- user / subreddit ----------

bool RedditClient::me(Redditor& out) {
  if (!requireWrite(_readOnly, _err, "me")) return false;
  HttpResponse r = apiGet("/api/v1/me");
  if (_err.code != RedditError::Ok) return false;
  if (!parseRedditor(r.body, out)) {
    setErr(_err, RedditError::JsonError, "me: failed to parse redditor JSON");
    return false;
  }
  return true;
}

bool RedditClient::userAbout(const std::string& username, Redditor& out) {
  if (username.empty()) {
    setErr(_err, RedditError::InvalidArgs,
           "userAbout: username must not be empty");
    return false;
  }
  HttpResponse r = apiGet("/user/" + util::urlEncode(username) + "/about");
  if (_err.code != RedditError::Ok) return false;
  if (!parseRedditor(r.body, out)) {
    setErr(_err, RedditError::JsonError,
           "userAbout: failed to parse redditor JSON");
    return false;
  }
  return true;
}

std::vector<Submission> RedditClient::userSubmitted(
    const std::string& username, const ListingOptions& opt) {
  if (username.empty()) {
    setErr(_err, RedditError::InvalidArgs,
           "userSubmitted: username must not be empty");
    return {};
  }
  int limit = clampLimit(opt, _config);
  HttpResponse r = apiGet("/user/" + util::urlEncode(username) + "/submitted" +
                          listingQuery(opt, limit, false));
  if (_err.code != RedditError::Ok) return {};
  std::vector<Submission> out;
  std::string after;
  if (!parseSubmissionListing(r.body, out, after,
                              static_cast<uint8_t>(limit))) {
    setErr(_err, RedditError::JsonError,
           "userSubmitted: failed to parse listing JSON");
    return {};
  }
  _lastAfter = after;
  return out;
}

std::vector<Comment> RedditClient::userComments(const std::string& username,
                                               const ListingOptions& opt) {
  if (username.empty()) {
    setErr(_err, RedditError::InvalidArgs,
           "userComments: username must not be empty");
    return {};
  }
  int limit = clampLimit(opt, _config);
  HttpResponse r = apiGet("/user/" + util::urlEncode(username) + "/comments" +
                          listingQuery(opt, limit, false));
  if (_err.code != RedditError::Ok) return {};
  std::vector<Comment> out;
  if (!parseUserCommentsListing(r.body, out)) {
    setErr(_err, RedditError::JsonError,
           "userComments: failed to parse comments listing JSON");
    return {};
  }
  _lastAfter.clear();  // cursor tracking is submission-listing only here
  return out;
}

bool RedditClient::subredditAbout(const std::string& subreddit,
                                  SubredditInfo& out) {
  std::string sub = normalizeSubreddit(subreddit);
  if (sub.empty()) {
    setErr(_err, RedditError::InvalidArgs,
           "subredditAbout: subreddit must not be empty");
    return false;
  }
  HttpResponse r = apiGet("/r/" + util::urlEncode(sub) + "/about");
  if (_err.code != RedditError::Ok) return false;
  if (!parseSubredditInfo(r.body, out)) {
    setErr(_err, RedditError::JsonError,
           "subredditAbout: failed to parse subreddit JSON");
    return false;
  }
  return true;
}

}  // namespace reddit

#include <unity.h>
#include <cstring>
#include "../helpers/test_helpers.h"
#include "../helpers/fixtures.h"
#include "RedditClient.h"

using namespace reddit;
using namespace reddittest;

static const char* UA = "esp32:test:v0.1.0 (by /u/testuser)";

static RedditConfig fastConfig() {
  RedditConfig c;
  c.minRequestIntervalMs = 0;  // no pacing sleeps in tests
  return c;
}

// Returns a client in auth mode with a cached token (queues token fetch).
static RedditClient authedClient(FakeTransport& t) {
  RedditClient c;
  TEST_ASSERT_TRUE(
      c.begin("id", "secret", "user", "pass", UA, t, fastConfig()));
  t.queuePost("access_token", 200, kTokenOkJson);
  // Trigger the lazy token fetch with a cheap call.
  t.queueGet("/api/v1/me", 200, kMeJson);
  Redditor r;
  TEST_ASSERT_TRUE(c.me(r));
  return c;
}

void setUp() { FakeClock::install(); }
void tearDown() { FakeClock::uninstall(); }

void test_begin_requires_transport_use() {
  FakeTransport t;
  RedditClient c;
  // No begin(): every call must fail cleanly, not crash.
  TEST_ASSERT_TRUE(c.hot("esp32").empty());
  TEST_ASSERT_EQUAL(RedditError::NotInitialized, c.lastError());
}

void test_begin_validates_args() {
  FakeTransport t;
  RedditClient c;
  TEST_ASSERT_FALSE(c.begin("", "s", "u", "p", UA, t, fastConfig()));
  TEST_ASSERT_EQUAL(RedditError::InvalidArgs, c.lastError());
  TEST_ASSERT_FALSE(c.beginReadOnly("", t, fastConfig()));
}

void test_hot_url_and_headers() {
  FakeTransport t;
  RedditClient c = authedClient(t);
  t.queueGet("/r/esp32/hot", 200, kListingJson);
  auto posts = c.hot("esp32");
  TEST_ASSERT_EQUAL_size_t(2, posts.size());
  TEST_ASSERT_EQUAL_STRING(
      "https://oauth.reddit.com/r/esp32/hot?raw_json=1&limit=10",
      t.lastRequest.url.c_str());
  TEST_ASSERT_EQUAL_STRING("Bearer tok_abc123",
                           t.header("Authorization").c_str());
  TEST_ASSERT_EQUAL_STRING(UA, t.header("User-Agent").c_str());
  TEST_ASSERT_EQUAL_STRING("t3_def456", c.lastAfter().c_str());
}

void test_subreddit_prefix_stripped() {
  FakeTransport t;
  RedditClient c = authedClient(t);
  t.queueGet("/r/esp32/hot", 200, kListingJson);
  c.hot("r/esp32");
  TEST_ASSERT_TRUE(t.lastRequest.url.find("/r/esp32/hot") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(t.lastRequest.url.find("/r/r/") == std::string::npos);
}

void test_top_adds_time_param() {
  FakeTransport t;
  RedditClient c = authedClient(t);
  t.queueGet("/r/esp32/top", 200, kListingJson);
  ListingOptions opt;
  opt.time = "week";
  c.top("esp32", opt);
  TEST_ASSERT_NOT_NULL(strstr(t.lastRequest.url.c_str(), "t=week"));
}

void test_limit_clamped() {
  FakeTransport t;
  RedditClient c = authedClient(t);
  t.queueGet("/r/esp32/hot", 200, kListingJson);
  ListingOptions opt;
  opt.limit = 500;  // over maxLimit(25) and Reddit cap(100)
  c.hot("esp32", opt);
  TEST_ASSERT_NOT_NULL(strstr(t.lastRequest.url.c_str(), "limit=25"));
}

void test_401_retries_once_with_fresh_token() {
  FakeTransport t;
  RedditClient c = authedClient(t);
  int callsBefore = t.calls;
  t.queueGet("/r/esp32/hot", 401, "");                       // dead token
  t.queuePost("access_token", 200, kTokenOkJson);             // re-auth
  t.queueGet("/r/esp32/hot", 200, kListingJson);              // retry ok
  auto posts = c.hot("esp32");
  TEST_ASSERT_EQUAL_size_t(2, posts.size());
  TEST_ASSERT_EQUAL(3, t.calls - callsBefore);  // 401 + token + retry
  TEST_ASSERT_EQUAL(RedditError::Ok, c.lastError());
}

void test_403_never_retries() {
  FakeTransport t;
  RedditClient c = authedClient(t);
  int callsBefore = t.calls;
  t.queueGet("/r/esp32/hot", 403, "");
  TEST_ASSERT_TRUE(c.hot("esp32").empty());
  TEST_ASSERT_EQUAL(RedditError::Forbidden, c.lastError());
  TEST_ASSERT_EQUAL(1, t.calls - callsBefore);  // no retry
}

void test_429_backoff_fail_fast() {
  FakeTransport t;
  RedditClient c = authedClient(t);
  reddit::HttpResponse r;
  r.status = 429;
  r.retryAfter = "120";
  t.queue("GET", "/r/esp32/hot", std::move(r));
  TEST_ASSERT_TRUE(c.hot("esp32").empty());
  TEST_ASSERT_EQUAL(RedditError::RateLimited, c.lastError());
  int callsBefore = t.calls;
  // Still inside the 120s backoff: no HTTP request may go out.
  TEST_ASSERT_TRUE(c.hot("esp32").empty());
  TEST_ASSERT_EQUAL(callsBefore, t.calls);
  TEST_ASSERT_EQUAL(RedditError::RateLimited, c.lastError());
}

void test_readonly_uses_www_host_no_auth_header() {
  FakeTransport t;
  RedditClient c;
  TEST_ASSERT_TRUE(c.beginReadOnly(UA, t, fastConfig()));
  TEST_ASSERT_FALSE(c.isAuthenticated());
  t.queueGet("/r/esp32/hot", 200, kListingJson);
  TEST_ASSERT_EQUAL_size_t(2, c.hot("esp32").size());
  TEST_ASSERT_EQUAL_STRING(
      "https://www.reddit.com/r/esp32/hot?raw_json=1&limit=10",
      t.lastRequest.url.c_str());
  TEST_ASSERT_EQUAL_STRING("", t.header("Authorization").c_str());
}

void test_readonly_rejects_mutations() {
  FakeTransport t;
  RedditClient c;
  c.beginReadOnly(UA, t, fastConfig());
  TEST_ASSERT_FALSE(c.vote("t3_abc", 1));
  TEST_ASSERT_EQUAL(RedditError::InvalidArgs, c.lastError());
  TEST_ASSERT_EQUAL(0, t.calls);  // no network attempt at all
  TEST_ASSERT_FALSE(c.submitText("test", "t", "b"));
  TEST_ASSERT_FALSE(c.comment("t3_abc", "hi"));
}

void test_vote_validation_and_body() {
  FakeTransport t;
  RedditClient c = authedClient(t);
  TEST_ASSERT_FALSE(c.vote("t3_abc", 5));
  TEST_ASSERT_EQUAL(RedditError::InvalidArgs, c.lastError());
  TEST_ASSERT_FALSE(c.vote("", 1));
  t.queuePost("/api/vote", 200, "");
  TEST_ASSERT_TRUE(c.vote("t3_abc123", -1));
  TEST_ASSERT_NOT_NULL(strstr(t.lastRequest.body.c_str(), "id=t3_abc123"));
  TEST_ASSERT_NOT_NULL(strstr(t.lastRequest.body.c_str(), "dir=-1"));
  TEST_ASSERT_EQUAL_STRING("application/x-www-form-urlencoded",
                           t.header("Content-Type").c_str());
}

void test_submit_title_truncated() {
  FakeTransport t;
  RedditClient c = authedClient(t);
  t.queuePost("/api/submit", 200, kMutationOkJson);
  std::string longTitle(400, 'x');
  std::string fullname;
  TEST_ASSERT_TRUE(c.submitText("test", longTitle, "body", &fullname));
  TEST_ASSERT_EQUAL_STRING("t3_n3wp0st", fullname.c_str());
  // title= param value must be exactly 300 x's
  const char* p = strstr(t.lastRequest.body.c_str(), "title=");
  TEST_ASSERT_NOT_NULL(p);
  p += 6;
  int n = 0;
  while (*p == 'x') { ++n; ++p; }
  TEST_ASSERT_EQUAL_INT(300, n);
}

void test_spam_blocked_mapping() {
  FakeTransport t;
  RedditClient c = authedClient(t);
  t.queuePost("/api/comment", 200, kMutationRateLimitJson);
  TEST_ASSERT_FALSE(c.comment("t3_abc", "hi"));
  TEST_ASSERT_EQUAL(RedditError::SpamBlocked, c.lastError());
  TEST_ASSERT_TRUE(c.lastErrorMessage().find("8 minutes") !=
                   std::string::npos);
}

void test_get_comments_tree() {
  FakeTransport t;
  RedditClient c = authedClient(t);
  t.queueGet("/comments/abc123", 200, kCommentsJson);
  auto comments = c.getComments("esp32", "abc123");
  TEST_ASSERT_EQUAL_size_t(2, comments.size());
  TEST_ASSERT_EQUAL_size_t(1, comments[0].replies.size());
  TEST_ASSERT_NOT_NULL(strstr(t.lastRequest.url.c_str(), "sort=confidence"));
}

void test_subreddit_about() {
  FakeTransport t;
  RedditClient c = authedClient(t);
  t.queueGet("/r/esp32/about", 200, kSubredditAboutJson);
  SubredditInfo s;
  TEST_ASSERT_TRUE(c.subredditAbout("esp32", s));
  TEST_ASSERT_EQUAL_STRING("esp32", s.displayName.c_str());
  TEST_ASSERT_EQUAL_INT(123456, (int)s.subscribers);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_begin_requires_transport_use);
  RUN_TEST(test_begin_validates_args);
  RUN_TEST(test_hot_url_and_headers);
  RUN_TEST(test_subreddit_prefix_stripped);
  RUN_TEST(test_top_adds_time_param);
  RUN_TEST(test_limit_clamped);
  RUN_TEST(test_401_retries_once_with_fresh_token);
  RUN_TEST(test_403_never_retries);
  RUN_TEST(test_429_backoff_fail_fast);
  RUN_TEST(test_readonly_uses_www_host_no_auth_header);
  RUN_TEST(test_readonly_rejects_mutations);
  RUN_TEST(test_vote_validation_and_body);
  RUN_TEST(test_submit_title_truncated);
  RUN_TEST(test_spam_blocked_mapping);
  RUN_TEST(test_get_comments_tree);
  RUN_TEST(test_subreddit_about);
  return UNITY_END();
}

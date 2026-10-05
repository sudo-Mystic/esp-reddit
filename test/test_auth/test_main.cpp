#include <unity.h>
#include <cstring>
#include "../helpers/test_helpers.h"
#include "../helpers/fixtures.h"
#include "RedditAuth.h"

using namespace reddit;
using namespace reddittest;

static const char* UA = "esp32:test:v0.1.0 (by /u/testuser)";

void setUp() { FakeClock::install(); }
void tearDown() { FakeClock::uninstall(); }

void test_configure_rejects_empty() {
  RedditAuth a;
  TEST_ASSERT_FALSE(a.configure("", "s", "u", "p", UA));
  TEST_ASSERT_FALSE(a.configure("id", "", "u", "p", UA));
  TEST_ASSERT_FALSE(a.configure("id", "s", "", "p", UA));
  TEST_ASSERT_FALSE(a.configure("id", "s", "u", "", UA));
  TEST_ASSERT_FALSE(a.configure("id", "s", "u", "p", ""));
  TEST_ASSERT_FALSE(a.isConfigured());
}

void test_token_request_shape() {
  FakeTransport t;
  t.queuePost("access_token", 200, kTokenOkJson);
  RedditAuth a;
  TEST_ASSERT_TRUE(a.configure("myid", "mysecret", "user", "p@ss w0rd", UA));
  TEST_ASSERT_TRUE(a.ensureToken(t));
  TEST_ASSERT_EQUAL(1, t.calls);
  TEST_ASSERT_EQUAL_STRING("https://www.reddit.com/api/v1/access_token",
                           t.lastRequest.url.c_str());
  TEST_ASSERT_EQUAL_STRING("Basic bXlpZDpteXNlY3JldA==",
                           t.header("Authorization").c_str());
  TEST_ASSERT_EQUAL_STRING(UA, t.header("User-Agent").c_str());
  TEST_ASSERT_EQUAL_STRING("application/x-www-form-urlencoded",
                           t.header("Content-Type").c_str());
  // password with space must be form-encoded in the body
  TEST_ASSERT_NOT_NULL(
      strstr(t.lastRequest.body.c_str(), "password=p%40ss+w0rd"));
  TEST_ASSERT_EQUAL_STRING("Bearer tok_abc123",
                           a.authorizationHeader().c_str());
}

void test_token_cached_until_margin() {
  FakeTransport t;
  t.queuePost("access_token", 200, kTokenOkJson);
  RedditAuth a;
  a.configure("id", "secret", "user", "pass", UA);
  TEST_ASSERT_TRUE(a.ensureToken(t, 300));
  TEST_ASSERT_EQUAL(1, t.calls);
  // 50 minutes later: still valid (margin 300s of 3600s).
  FakeClock::advance(50 * 60 * 1000);
  TEST_ASSERT_TRUE(a.ensureToken(t, 300));
  TEST_ASSERT_EQUAL(1, t.calls);  // no second HTTP call
  // 56 minutes later: inside the margin -> refresh.
  FakeClock::advance(6 * 60 * 1000);
  t.queuePost("access_token", 200, kTokenOkJson);
  TEST_ASSERT_TRUE(a.ensureToken(t, 300));
  TEST_ASSERT_EQUAL(2, t.calls);
}

void test_invalid_grant() {
  FakeTransport t;
  t.queuePost("access_token", 200, kTokenInvalidGrantJson);
  RedditAuth a;
  a.configure("id", "secret", "user", "pass", UA);
  TEST_ASSERT_FALSE(a.ensureToken(t));
  TEST_ASSERT_EQUAL(RedditError::AuthFailed, a.lastError());
  TEST_ASSERT_EQUAL_STRING("invalid_grant", a.lastErrorMessage().c_str());
  TEST_ASSERT_EQUAL_STRING("", a.authorizationHeader().c_str());
}

void test_401_bad_client_credentials() {
  FakeTransport t;
  t.queuePost("access_token", 401, "");
  RedditAuth a;
  a.configure("id", "secret", "user", "pass", UA);
  TEST_ASSERT_FALSE(a.ensureToken(t));
  TEST_ASSERT_EQUAL(RedditError::AuthFailed, a.lastError());
}

void test_transport_failure() {
  FakeTransport t;
  reddit::HttpResponse r;
  r.status = -1;
  r.timeout = true;
  t.queue("POST", "access_token", std::move(r));
  RedditAuth a;
  a.configure("id", "secret", "user", "pass", UA);
  TEST_ASSERT_FALSE(a.ensureToken(t));
  TEST_ASSERT_EQUAL(RedditError::HttpTimeout, a.lastError());
}

void test_invalidate_drops_token() {
  FakeTransport t;
  t.queuePost("access_token", 200, kTokenOkJson);
  RedditAuth a;
  a.configure("id", "secret", "user", "pass", UA);
  TEST_ASSERT_TRUE(a.ensureToken(t));
  a.invalidate();
  TEST_ASSERT_EQUAL_STRING("", a.authorizationHeader().c_str());
  t.queuePost("access_token", 200, kTokenOkJson);
  TEST_ASSERT_TRUE(a.ensureToken(t));
  TEST_ASSERT_EQUAL(2, t.calls);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_configure_rejects_empty);
  RUN_TEST(test_token_request_shape);
  RUN_TEST(test_token_cached_until_margin);
  RUN_TEST(test_invalid_grant);
  RUN_TEST(test_401_bad_client_credentials);
  RUN_TEST(test_transport_failure);
  RUN_TEST(test_invalidate_drops_token);
  return UNITY_END();
}

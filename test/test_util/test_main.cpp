#include <unity.h>
#include "../helpers/test_helpers.h"
#include "RedditUtil.h"

using namespace reddit::util;

void setUp() {}
void tearDown() {}

void test_base64_rfc4648_vectors() {
  TEST_ASSERT_EQUAL_STRING("", base64Encode("").c_str());
  TEST_ASSERT_EQUAL_STRING("Zg==", base64Encode("f").c_str());
  TEST_ASSERT_EQUAL_STRING("Zm8=", base64Encode("fo").c_str());
  TEST_ASSERT_EQUAL_STRING("Zm9v", base64Encode("foo").c_str());
  TEST_ASSERT_EQUAL_STRING("Zm9vYmFy", base64Encode("foobar").c_str());
}

void test_base64_client_secret_pair() {
  // "abc:def" -> used for the OAuth Basic header.
  TEST_ASSERT_EQUAL_STRING("YWJjOmRlZg==",
                           base64Encode("abc:def").c_str());
}

void test_form_encode() {
  TEST_ASSERT_EQUAL_STRING("hello+world", formEncode("hello world").c_str());
  TEST_ASSERT_EQUAL_STRING("a%26b%3Dc", formEncode("a&b=c").c_str());
  TEST_ASSERT_EQUAL_STRING("plain-_.~09AZaz",
                           formEncode("plain-_.~09AZaz").c_str());
}

void test_url_encode_uses_pct20() {
  TEST_ASSERT_EQUAL_STRING("hello%20world", urlEncode("hello world").c_str());
  TEST_ASSERT_EQUAL_STRING("a%2Fb", urlEncode("a/b").c_str());
}

void test_build_form_body() {
  std::string b = buildFormBody(
      {{"grant_type", "password"}, {"username", "u s e r"}});
  TEST_ASSERT_EQUAL_STRING("grant_type=password&username=u+s+e+r", b.c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_base64_rfc4648_vectors);
  RUN_TEST(test_base64_client_secret_pair);
  RUN_TEST(test_form_encode);
  RUN_TEST(test_url_encode_uses_pct20);
  RUN_TEST(test_build_form_body);
  return UNITY_END();
}

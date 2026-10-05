#include <unity.h>
#include "../helpers/test_helpers.h"
#include "../helpers/fixtures.h"
#include "RedditModels.h"

using namespace reddit;
using namespace reddittest;

void setUp() {}
void tearDown() {}

void test_listing_parses_fields() {
  std::vector<Submission> out;
  std::string after;
  TEST_ASSERT_TRUE(parseSubmissionListing(kListingJson, out, after, 25));
  TEST_ASSERT_EQUAL_size_t(2, out.size());
  const Submission& s = out[0];
  TEST_ASSERT_EQUAL_STRING("abc123", s.id.c_str());
  TEST_ASSERT_EQUAL_STRING("t3_abc123", s.fullname.c_str());
  TEST_ASSERT_EQUAL_STRING("Hello ESP32", s.title.c_str());
  TEST_ASSERT_EQUAL_STRING("body text", s.selftext.c_str());
  TEST_ASSERT_EQUAL_STRING("someuser", s.author.c_str());
  TEST_ASSERT_EQUAL_STRING("esp32", s.subreddit.c_str());
  TEST_ASSERT_EQUAL_STRING("t5_2qh33", s.subredditId.c_str());
  TEST_ASSERT_EQUAL_STRING("Project", s.flairText.c_str());
  TEST_ASSERT_EQUAL_INT(42, (int)s.score);
  TEST_ASSERT_EQUAL_INT(7, (int)s.numComments);
  TEST_ASSERT_TRUE(s.stickied);
  TEST_ASSERT_TRUE(s.isSelf);
  TEST_ASSERT_FALSE(s.over18);
  TEST_ASSERT_EQUAL_STRING("t3_def456", after.c_str());
}

void test_listing_max_items_cap() {
  std::vector<Submission> out;
  std::string after;
  TEST_ASSERT_TRUE(parseSubmissionListing(kListingJson, out, after, 1));
  TEST_ASSERT_EQUAL_size_t(1, out.size());
}

void test_listing_fullname_fallback_from_id() {
  // Second child has id but no "name": fullname must become t3_<id>.
  std::vector<Submission> out;
  std::string after;
  TEST_ASSERT_TRUE(parseSubmissionListing(kListingJson, out, after, 25));
  TEST_ASSERT_EQUAL_STRING("t3_def456", out[1].fullname.c_str());
  TEST_ASSERT_EQUAL_STRING("Second post (no name field)",
                           out[1].title.c_str());
}

void test_listing_bad_json() {
  std::vector<Submission> out;
  std::string after;
  TEST_ASSERT_FALSE(parseSubmissionListing("{not json", out, after, 25));
  TEST_ASSERT_FALSE(parseSubmissionListing(R"({"kind":"x"})", out, after, 25));
}

void test_comments_tree() {
  Submission post;
  std::vector<Comment> comments;
  TEST_ASSERT_TRUE(parseCommentsResponse(kCommentsJson, post, comments, 6));
  TEST_ASSERT_EQUAL_STRING("Hello ESP32", post.title.c_str());
  TEST_ASSERT_EQUAL_STRING("t3_abc123", post.fullname.c_str());
  TEST_ASSERT_EQUAL_size_t(2, comments.size());
  // "more" child must be skipped: first comment has exactly 1 reply.
  TEST_ASSERT_EQUAL_size_t(1, comments[0].replies.size());
  TEST_ASSERT_EQUAL_STRING("Reply here", comments[0].replies[0].body.c_str());
  TEST_ASSERT_EQUAL_INT(1, comments[0].replies[0].depth);
  TEST_ASSERT_EQUAL_STRING("t1_c1", comments[0].replies[0].parentId.c_str());
  TEST_ASSERT_EQUAL_STRING("Second top-level", comments[1].body.c_str());
  TEST_ASSERT_TRUE(comments[1].replies.empty());
}

void test_comments_depth_cap_zero() {
  Submission post;
  std::vector<Comment> comments;
  TEST_ASSERT_TRUE(parseCommentsResponse(kCommentsJson, post, comments, 0));
  TEST_ASSERT_EQUAL_size_t(2, comments.size());
  TEST_ASSERT_TRUE(comments[0].replies.empty());  // no recursion at depth 0
}

void test_redditor_both_shapes() {
  Redditor r;
  TEST_ASSERT_TRUE(parseRedditor(kMeJson, r));  // flat /api/v1/me shape
  TEST_ASSERT_EQUAL_STRING("testuser", r.name.c_str());
  TEST_ASSERT_EQUAL_STRING("t2_xyz", r.id.c_str());
  TEST_ASSERT_EQUAL_INT(100, (int)r.linkKarma);
  TEST_ASSERT_EQUAL_INT(250, (int)r.commentKarma);
  TEST_ASSERT_TRUE(r.isMod);
  TEST_ASSERT_FALSE(r.isGold);

  Redditor r2;
  TEST_ASSERT_TRUE(parseRedditor(kUserAboutJson, r2));  // kind t2 envelope
  TEST_ASSERT_EQUAL_STRING("otheruser", r2.name.c_str());
  TEST_ASSERT_EQUAL_STRING("t2_abc999", r2.id.c_str());  // prefix normalized
}

void test_subreddit_info() {
  SubredditInfo s;
  TEST_ASSERT_TRUE(parseSubredditInfo(kSubredditAboutJson, s));
  TEST_ASSERT_EQUAL_STRING("esp32", s.displayName.c_str());
  TEST_ASSERT_EQUAL_STRING("t5_2qh33", s.id.c_str());
  TEST_ASSERT_EQUAL_INT(123456, (int)s.subscribers);
  TEST_ASSERT_EQUAL_INT(789, (int)s.activeCount);
  TEST_ASSERT_EQUAL_STRING("any", s.submissionType.c_str());
}

void test_mutation_success() {
  std::string code, msg, id;
  TEST_ASSERT_TRUE(parseMutationResult(kMutationOkJson, code, msg, id));
  TEST_ASSERT_EQUAL_STRING("t3_n3wp0st", id.c_str());
}

void test_mutation_ratelimit() {
  std::string code, msg, id;
  TEST_ASSERT_FALSE(parseMutationResult(kMutationRateLimitJson, code, msg, id));
  TEST_ASSERT_EQUAL_STRING("RATELIMIT", code.c_str());
  TEST_ASSERT_TRUE(msg.find("8 minutes") != std::string::npos);
}

void test_mutation_api_error() {
  std::string code, msg, id;
  TEST_ASSERT_FALSE(parseMutationResult(kMutationApiErrorJson, code, msg, id));
  TEST_ASSERT_EQUAL_STRING("NO_TEXT", code.c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_listing_parses_fields);
  RUN_TEST(test_listing_max_items_cap);
  RUN_TEST(test_listing_fullname_fallback_from_id);
  RUN_TEST(test_listing_bad_json);
  RUN_TEST(test_comments_tree);
  RUN_TEST(test_comments_depth_cap_zero);
  RUN_TEST(test_redditor_both_shapes);
  RUN_TEST(test_subreddit_info);
  RUN_TEST(test_mutation_success);
  RUN_TEST(test_mutation_ratelimit);
  RUN_TEST(test_mutation_api_error);
  return UNITY_END();
}

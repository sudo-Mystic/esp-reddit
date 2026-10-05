#pragma once
// Canned Reddit JSON payloads for the parser tests.
#include <string>

namespace reddittest {

inline const std::string kListingJson = R"json({
  "kind": "Listing",
  "data": {
    "modhash": "",
    "dist": 2,
    "children": [
      {"kind": "t3", "data": {
        "id": "abc123", "name": "t3_abc123",
        "title": "Hello ESP32", "selftext": "body text",
        "url": "https://www.reddit.com/r/esp32/comments/abc123/hello_esp32/",
        "permalink": "/r/esp32/comments/abc123/hello_esp32/",
        "author": "someuser", "subreddit": "esp32", "subreddit_id": "t5_2qh33",
        "score": 42, "num_comments": 7, "created_utc": 1728000000.0,
        "upvote_ratio": 0.95, "over_18": false, "spoiler": false,
        "stickied": true, "locked": false, "is_self": true,
        "saved": false, "archived": false, "link_flair_text": "Project",
        "thumbnail": "self", "domain": "self.esp32", "edited": false,
        "extra_unfiltered_field": "should be dropped by the filter"
      }},
      {"kind": "t3", "data": {
        "id": "def456",
        "title": "Second post (no name field)",
        "author": "otheruser", "subreddit": "esp32",
        "score": 10, "num_comments": 0, "created_utc": 1728000100.0
      }}
    ],
    "after": "t3_def456",
    "before": null
  }
})json";

inline const std::string kCommentsJson = R"json([
  {"kind": "Listing", "data": {"children": [
    {"kind": "t3", "data": {
      "id": "abc123", "name": "t3_abc123", "title": "Hello ESP32",
      "author": "someuser", "subreddit": "esp32", "score": 42,
      "num_comments": 2, "created_utc": 1728000000.0
    }}
  ], "after": null, "before": null}},
  {"kind": "Listing", "data": {"children": [
    {"kind": "t1", "data": {
      "id": "c1", "name": "t1_c1", "body": "First!", "author": "alice",
      "subreddit": "esp32", "link_id": "t3_abc123", "parent_id": "t3_abc123",
      "score": 5, "created_utc": 1728000010.0, "depth": 0,
      "replies": {"kind": "Listing", "data": {"children": [
        {"kind": "t1", "data": {
          "id": "c2", "name": "t1_c2", "body": "Reply here",
          "author": "bob", "subreddit": "esp32",
          "link_id": "t3_abc123", "parent_id": "t1_c1",
          "score": 2, "created_utc": 1728000020.0, "depth": 1,
          "replies": ""
        }},
        {"kind": "more", "data": {"children": ["c9", "c10"], "count": 2}}
      ], "after": null}}
    }},
    {"kind": "t1", "data": {
      "id": "c3", "name": "t1_c3", "body": "Second top-level",
      "author": "carol", "subreddit": "esp32",
      "link_id": "t3_abc123", "parent_id": "t3_abc123",
      "score": 1, "created_utc": 1728000030.0, "depth": 0,
      "replies": ""
    }}
  ], "after": null}}
])json";

inline const std::string kMeJson = R"json({
  "id": "t2_xyz", "name": "testuser",
  "link_karma": 100, "comment_karma": 250,
  "created_utc": 1600000000.0, "is_gold": false, "is_mod": true
})json";

inline const std::string kUserAboutJson = R"json({
  "kind": "t2",
  "data": {"id": "abc999", "name": "otheruser", "link_karma": 10,
           "comment_karma": 20, "created_utc": 1610000000.0}
})json";

inline const std::string kSubredditAboutJson = R"json({
  "kind": "t5",
  "data": {"name": "t5_2qh33", "display_name": "esp32",
           "title": "ESP32", "public_description": "The ESP32 subreddit.",
           "subscribers": 123456, "accounts_active": 789,
           "over18": false, "submission_type": "any"}
})json";

inline const std::string kMutationOkJson = R"json({
  "json": {"errors": [],
           "data": {"id": "n3wp0st", "name": "t3_n3wp0st",
                    "url": "/r/test/comments/n3wp0st/title/"}}
})json";

inline const std::string kMutationRateLimitJson = R"json({
  "json": {"errors": [["RATELIMIT",
    "you are doing that too much. try again in 8 minutes.", "ratelimit"]],
           "data": {}}
})json";

inline const std::string kMutationApiErrorJson = R"json({
  "json": {"errors": [["NO_TEXT", "we need something here", "text"]],
           "data": {}}
})json";

inline const std::string kTokenOkJson = R"json({
  "access_token": "tok_abc123", "token_type": "bearer",
  "expires_in": 3600, "scope": "*"
})json";

inline const std::string kTokenInvalidGrantJson =
    R"json({"error": "invalid_grant"})json";

}  // namespace reddittest

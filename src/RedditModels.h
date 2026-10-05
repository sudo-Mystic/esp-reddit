#pragma once
// Data models + JSON parsing. All parsing is host-testable: it takes a
// JSON string and fills plain structs. ArduinoJson filtering keeps the
// memory footprint small on device.
#include <string>
#include <vector>
#include <stdint.h>

namespace reddit {

struct Submission {
  std::string id;          // base36 id, e.g. "abc123"
  std::string fullname;    // e.g. "t3_abc123"
  std::string title;
  std::string selftext;    // markdown body (empty for link posts)
  std::string url;
  std::string permalink;   // e.g. "/r/esp32/comments/abc123/slug/"
  std::string author;
  std::string subreddit;
  std::string subredditId;  // t5_...
  std::string flairText;
  long score = 0;
  long numComments = 0;
  double createdUtc = 0;  // Unix seconds
  double upvoteRatio = 0;
  bool over18 = false;
  bool spoiler = false;
  bool stickied = false;
  bool locked = false;
  bool isSelf = false;
  bool saved = false;
  bool archived = false;
};

struct Comment {
  std::string id;
  std::string fullname;  // t1_...
  std::string body;      // markdown
  std::string author;
  std::string subreddit;
  std::string linkId;    // parent submission fullname
  std::string parentId;  // direct parent fullname
  long score = 0;
  double createdUtc = 0;
  int depth = 0;
  bool scoreHidden = false;
  bool stickied = false;
  std::vector<Comment> replies;  // nested, depth-capped by the parser
};

struct Redditor {
  std::string id;  // t2_...
  std::string name;
  long linkKarma = 0;
  long commentKarma = 0;
  double createdUtc = 0;
  bool isGold = false;
  bool isMod = false;
};

struct SubredditInfo {
  std::string id;  // t5_...
  std::string displayName;
  std::string title;
  std::string publicDescription;
  std::string submissionType;  // "any" | "link" | "self"
  long subscribers = 0;
  long activeCount = 0;
  bool over18 = false;
};

// Parse a Listing envelope (kind:"Listing") into submissions.
// `after` receives the pagination cursor (empty when at the end).
// At most maxItems are extracted. Returns false on JSON error.
bool parseSubmissionListing(const std::string& json,
                            std::vector<Submission>& out, std::string& after,
                            uint8_t maxItems);

// Parse the two-element array returned by /r/{sub}/comments/{id}:
// [postListing, commentsListing]. `post` gets the submission, `comments`
// the top-level comment tree (replies nested, depth-capped).
bool parseCommentsResponse(const std::string& json, Submission& post,
                           std::vector<Comment>& comments, uint8_t maxDepth);

// Parse /api/v1/me or /user/{name}/about into a Redditor.
bool parseRedditor(const std::string& json, Redditor& out);

// Parse /r/{sub}/about into SubredditInfo.
bool parseSubredditInfo(const std::string& json, SubredditInfo& out);

// Parse a mutation envelope {"json":{"errors":[...],"data":{...}}}.
// Returns true when errors[] is empty (success). On failure fills errCode
// (e.g. "RATELIMIT") and errMessage, and returns false. outId receives
// data.id/name when present (e.g. new post or comment id).
bool parseMutationResult(const std::string& json, std::string& errCode,
                         std::string& errMessage, std::string& outId);

}  // namespace reddit

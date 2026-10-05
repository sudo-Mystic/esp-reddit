#pragma once
// RedditClient: the main API. PRAW-inspired, synchronous, blocking.
// Construct once, call begin(), then use. All methods are synchronous and
// pace themselves per RedditConfig::minRequestIntervalMs.
//
// Threading: not thread-safe. Use from one task.
// Memory: listing calls allocate at most config.maxLimit items.
#include <string>
#include <vector>
#include <stdint.h>
#include "RedditConfig.h"
#include "RedditError.h"
#include "RedditTransport.h"
#include "RedditAuth.h"
#include "RedditModels.h"

namespace reddit {

// Options for listing endpoints.
struct ListingOptions {
  int limit = 0;  // 0 = config default; clamped to config.maxLimit (<=100)
  std::string after;   // pagination cursor from lastAfter()
  std::string before;
  std::string time = "day";  // top/controversial: hour|day|week|month|year|all
  std::string sort = "confidence";  // comments: confidence|top|new|controversial|old|qa
  int depth = 0;  // comments: 0 = server default
};

class RedditClient {
 public:
  RedditClient() = default;

  // Full access via a Reddit "script" app (reddit.com/prefs/apps).
  // userAgent must follow "<platform>:<app>:<version> (by /u/<username>)".
  bool begin(const std::string& clientId, const std::string& clientSecret,
             const std::string& username, const std::string& password,
             const std::string& userAgent, ITransport& transport,
             const RedditConfig& config = RedditConfig());

  // Read-only mode: no credentials. Works for public listings, comments,
  // user profiles and subreddit info. Much stricter rate limits apply.
  bool beginReadOnly(const std::string& userAgent, ITransport& transport,
                     const RedditConfig& config = RedditConfig());

  bool isAuthenticated() const;  // false in read-only mode

  // ---- Listings (work read-only) ----
  std::vector<Submission> hot(const std::string& subreddit,
                               const ListingOptions& opt = ListingOptions());
  std::vector<Submission> newPosts(const std::string& subreddit,
                                   const ListingOptions& opt = ListingOptions());
  std::vector<Submission> rising(const std::string& subreddit,
                                 const ListingOptions& opt = ListingOptions());
  std::vector<Submission> controversial(const std::string& subreddit,
                                       const ListingOptions& opt = ListingOptions());
  std::vector<Submission> top(const std::string& subreddit,
                              const ListingOptions& opt = ListingOptions());

  // Cursor for the next page after a listing call (empty = end).
  const std::string& lastAfter() const { return _lastAfter; }

  // ---- Submission + comments ----
  // Fetch a single submission's metadata (no comments).
  bool getSubmission(const std::string& subreddit, const std::string& id36,
                     Submission& out);
  // Fetch the comment tree for a submission.
  std::vector<Comment> getComments(const std::string& subreddit,
                                   const std::string& id36,
                                   const ListingOptions& opt = ListingOptions());

  // ---- Mutations (auth required) ----
  // outFullname receives "t3_..." of the new post on success (may be null).
  bool submitText(const std::string& subreddit, const std::string& title,
                  const std::string& text, std::string* outFullname = nullptr);
  bool submitLink(const std::string& subreddit, const std::string& title,
                  const std::string& url, std::string* outFullname = nullptr);
  // dir: 1 = upvote, -1 = downvote, 0 = clear vote. fullname like "t3_abc".
  bool vote(const std::string& fullname, int dir);
  bool save(const std::string& fullname);
  bool unsave(const std::string& fullname);
  bool del(const std::string& fullname);  // delete own post/comment
  bool editText(const std::string& fullname, const std::string& text);
  // Reply to a post (parent "t3_...") or a comment (parent "t1_...").
  // outFullname receives "t1_..." of the new comment (may be null).
  bool comment(const std::string& parentFullname, const std::string& text,
               std::string* outFullname = nullptr);
  bool subscribe(const std::string& subreddit, bool doSubscribe);

  // ---- User / subreddit ----
  bool me(Redditor& out);  // auth required
  bool userAbout(const std::string& username, Redditor& out);
  std::vector<Submission> userSubmitted(
      const std::string& username, const ListingOptions& opt = ListingOptions());
  std::vector<Comment> userComments(
      const std::string& username, const ListingOptions& opt = ListingOptions());
  bool subredditAbout(const std::string& subreddit, SubredditInfo& out);

  // ---- Errors ----
  RedditError lastError() const { return _err.code; }
  const std::string& lastErrorMessage() const { return _err.message; }
  int lastHttpStatus() const { return _err.httpStatus; }
  const ErrorInfo& errorInfo() const { return _err; }
  void clearError() { _err = ErrorInfo(); }

 private:
  // Low-level helpers used by every endpoint.
  bool requireAuth();
  bool pace();  // enforce minRequestIntervalMs + rate-limit backoff
  HttpResponse apiGet(const std::string& path);
  HttpResponse apiPost(const std::string& path, const std::string& formBody);
  bool checkHttp(const HttpResponse& r);  // maps status -> ErrorInfo, handles 401 retry
  std::vector<Submission> listing(const std::string& path, const ListingOptions& opt);

  std::string baseUrl() const;  // oauth host or www host in read-only mode

  RedditConfig _config;
  RedditAuth _auth;
  ITransport* _transport = nullptr;
  bool _readOnly = false;
  std::string _userAgent;
  ErrorInfo _err;
  std::string _lastAfter;
  uint32_t _lastRequestMs = 0;
  uint32_t _backoffUntilMs = 0;  // 0 = no backoff
  int _lastRemaining = -1;       // last seen x-ratelimit-remaining
};

}  // namespace reddit

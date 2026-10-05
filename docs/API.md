# API Reference

Generated from the public headers in `src/`. Include `<esp_reddit.h>` and use the `reddit::` namespace.

Conventions:

- All methods are **synchronous and blocking**. They pace themselves per `RedditConfig::minRequestIntervalMs`.
- **Not thread-safe.** Use from one task.
- Every public method sets the error state. Check `lastError()` after any call that returns `false` or an empty result.
- Listing calls allocate at most `config.maxLimit` items.
- `begin()` must succeed before any other call; `beginReadOnly()` is enough for read-only endpoints.

## Initialization

### `bool begin(clientId, clientSecret, username, password, userAgent, transport, config)`

Full access via a Reddit "script" app. See [SETUP.md](SETUP.md) for creating one.

```cpp
bool begin(const std::string& clientId, const std::string& clientSecret,
           const std::string& username, const std::string& password,
           const std::string& userAgent, ITransport& transport,
           const RedditConfig& config = RedditConfig());
```

- `userAgent` must follow `<platform>:<app>:<version> (by /u/<username>)`.
- `transport` is stored by pointer; it must outlive the client.
- Returns `false` on failure. Typical errors: `AuthFailed` (bad credentials, 2FA enabled, bad client secret), `TransportError` (no network / TLS failure, usually unsynced clock), `InvalidArgs` (empty argument).

### `bool beginReadOnly(userAgent, transport, config)`

```cpp
bool beginReadOnly(const std::string& userAgent, ITransport& transport,
                   const RedditConfig& config = RedditConfig());
```

No credentials. Works for public listings, comments, user profiles, and subreddit info. Stricter rate limits apply (~30 req/min per IP). Mutations and `me()` will fail with `NotInitialized`-style auth errors; call `requireAuth` internally returns false, surfaced as an auth error.

### `bool isAuthenticated() const`

Returns `false` in read-only mode.

## Listings

All five work in read-only mode. They return `std::vector<Submission>` (empty on failure) and record the pagination cursor retrievable via `lastAfter()`.

```cpp
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
```

- `subreddit`: name without the `r/` prefix, e.g. `"esp32"`. Case-insensitive.
- `opt.limit`: 0 means config default; clamped to `config.maxLimit` (max 100 per Reddit).
- `opt.time`: for `top`/`controversial`: `hour`, `day` (default), `week`, `month`, `year`, `all`.
- `opt.after` / `opt.before`: pagination cursors; use `lastAfter()` from the previous call.
- Error cases: `InvalidArgs` (empty subreddit), `NotFound` (no such subreddit), `Forbidden` (private/quarantined), `RateLimited`, `TransportError`, `JsonError`.

### `const std::string& lastAfter() const`

Cursor for the next page after a listing call. Empty string means end of listing. Pass it as `ListingOptions.after` on the next call.

### `struct ListingOptions`

```cpp
struct ListingOptions {
  int limit = 0;              // 0 = config default; clamped to config.maxLimit (<=100)
  std::string after;          // pagination cursor from lastAfter()
  std::string before;
  std::string time = "day";   // top/controversial: hour|day|week|month|year|all
  std::string sort = "confidence";  // comments: confidence|top|new|controversial|old|qa
  int depth = 0;              // comments: 0 = server default
};
```

## Submission + comments

### `bool getSubmission(subreddit, id36, out)`

Fetches a single submission's metadata (no comments). `id36` is the bare base36 id, e.g. `"abc123"`.

```cpp
bool getSubmission(const std::string& subreddit, const std::string& id36,
                   Submission& out);
```

Error cases: `InvalidArgs`, `NotFound`, `Forbidden`, `RateLimited`, `TransportError`, `JsonError`.

### `std::vector<Comment> getComments(subreddit, id36, opt)`

Fetches the comment tree. Top-level comments are returned in order; each `Comment.replies` holds nested replies, depth-capped by `config.maxCommentDepth`.

```cpp
std::vector<Comment> getComments(const std::string& subreddit,
                                 const std::string& id36,
                                 const ListingOptions& opt = ListingOptions());
```

- `opt.sort`: `confidence` (default), `top`, `new`, `controversial`, `old`, `qa`.
- `opt.depth`: 0 means server default.
- `opt.limit`: caps top-level comments.
- Error cases: `InvalidArgs`, `NotFound`, `Forbidden`, `RateLimited`, `TransportError`, `JsonError`.

## Mutations (auth required)

All return `bool`: `true` on success. On failure inspect `lastError()`, `lastErrorMessage()`, and `lastHttpStatus()`. Note that Reddit returns HTTP 200 even for failed mutations; the client parses `json.errors` and reports `ApiError` or `SpamBlocked` accordingly.

### `bool submitText(subreddit, title, text, outFullname)`

```cpp
bool submitText(const std::string& subreddit, const std::string& title,
                const std::string& text, std::string* outFullname = nullptr);
```

Submits a self post. On success `*outFullname` receives the new post's fullname (`t3_...`). Pass `nullptr` if you do not need it.

### `bool submitLink(subreddit, title, url, outFullname)`

```cpp
bool submitLink(const std::string& subreddit, const std::string& title,
                const std::string& url, std::string* outFullname = nullptr);
```

Submits a link post. Same `outFullname` contract as `submitText`.

### `bool vote(fullname, dir)`

```cpp
bool vote(const std::string& fullname, int dir);
```

- `dir`: `1` = upvote, `-1` = downvote, `0` = clear vote.
- `fullname`: e.g. `"t3_abc123"` for a post, `"t1_ab12cd"` for a comment.

### `bool save(fullname)` / `bool unsave(fullname)`

Save or unsave a post/comment by fullname.

### `bool del(fullname)`

Deletes your own post or comment.

### `bool editText(fullname, text)`

Replaces the markdown body of your own post/comment.

### `bool comment(parentFullname, text, outFullname)`

```cpp
bool comment(const std::string& parentFullname, const std::string& text,
             std::string* outFullname = nullptr);
```

Replies to a post (`t3_...`) or a comment (`t1_...`). On success `*outFullname` receives the new comment's fullname (`t1_...`).

### `bool subscribe(subreddit, doSubscribe)`

Subscribes (`true`) or unsubscribes (`false`) the authenticated account.

Mutation error cases: auth errors when not authenticated, `InvalidArgs` (empty title/text/fullname), `Forbidden` (subreddit posting rules, karma/age gates), `SpamBlocked` (per-account write cooldown, message states the wait), `ApiError` (other `json.errors`, check `errorInfo().redditCode`), `RateLimited`, `TransportError`.

## User / subreddit

### `bool me(out)` (auth required)

```cpp
bool me(Redditor& out);
```

Fills `out` with the authenticated account's profile.

### `bool userAbout(username, out)`

Public profile: karma, account age. Works read-only.

### `std::vector<Submission> userSubmitted(username, opt)`

The user's post history as a listing. Works read-only. Paginate with `lastAfter()`.

### `std::vector<Comment> userComments(username, opt)`

The user's comment history. Works read-only. Paginate with `lastAfter()`.

### `bool subredditAbout(subreddit, out)`

Fills `SubredditInfo` (subscribers, description, submission type, NSFW flag). Works read-only. Error cases: `NotFound`, `Forbidden` (private), `RateLimited`, `TransportError`, `JsonError`.

## Models

### `struct Submission`

`id` (base36, e.g. `"abc123"`), `fullname` (`"t3_abc123"`), `title`, `selftext` (markdown, empty for link posts), `url`, `permalink` (e.g. `"/r/esp32/comments/abc123/slug/"`), `author`, `subreddit`, `subredditId` (`t5_...`), `flairText`, `score` (long), `numComments` (long), `createdUtc` (Unix seconds, double), `upvoteRatio` (double), `over18`, `spoiler`, `stickied`, `locked`, `isSelf`, `saved`, `archived` (bool).

### `struct Comment`

`id`, `fullname` (`t1_...`), `body` (markdown), `author`, `subreddit`, `linkId` (parent submission fullname), `parentId` (direct parent fullname), `score` (long), `createdUtc` (double), `depth` (int), `scoreHidden`, `stickied` (bool), `replies` (`std::vector<Comment>`, nested, depth-capped).

### `struct Redditor`

`id` (`t2_...`), `name`, `linkKarma` (long), `commentKarma` (long), `createdUtc` (double), `isGold`, `isMod` (bool).

### `struct SubredditInfo`

`id` (`t5_...`), `displayName`, `title`, `publicDescription`, `submissionType` (`"any"`, `"link"`, or `"self"`), `subscribers` (long), `activeCount` (long), `over18` (bool).

## Errors

### `RedditError lastError() const`

The error code from the last call. `RedditError::Ok` means no error.

### `const std::string& lastErrorMessage() const`

Human-readable message for the last error.

### `int lastHttpStatus() const`

HTTP status of the last request (0 when the failure was below HTTP).

### `const ErrorInfo& errorInfo() const`

Full detail:

```cpp
struct ErrorInfo {
  RedditError code = RedditError::Ok;
  int httpStatus = 0;
  std::string message;
  std::string redditCode;  // e.g. "RATELIMIT", "NO_TEXT" from json.errors
};
```

### `void clearError()`

Resets the error state to `Ok`.

### `RedditError` values

| Value | Meaning |
|---|---|
| `Ok` | No error |
| `NotInitialized` | `begin()` was never called (or failed) |
| `InvalidArgs` | Empty or invalid argument passed by the caller |
| `AuthFailed` | Token request rejected: bad credentials, bad client secret, 2FA enabled |
| `TokenRefreshFailed` | Re-authentication failed mid-session |
| `TransportError` | TCP/TLS/DNS failure below HTTP (often unsynced clock) |
| `HttpTimeout` | Request exceeded `config.httpTimeoutMs` |
| `HttpError` | Other 4xx/5xx; see `ErrorInfo.httpStatus` |
| `Unauthorized` | HTTP 401: bad/expired token. The client re-authenticates and retries once automatically |
| `Forbidden` | HTTP 403: valid token, action not allowed (private sub, karma/age gate) |
| `NotFound` | HTTP 404 |
| `RateLimited` | HTTP 429, or the `x-ratelimit-remaining` budget ran out |
| `SpamBlocked` | `RATELIMIT` inside `json.errors`: per-account write cooldown, message states the wait |
| `JsonError` | Response not parseable, including out-of-memory during parse |
| `ApiError` | `json.errors[]` non-empty on a mutation; `ErrorInfo.redditCode` has the Reddit code |

`const char* errorName(RedditError e)` returns the enum name as a string, useful for logging.

## Config

`RedditConfig` is passed to `begin()`/`beginReadOnly()` and to `EspTransport`'s constructor. All limits keep RAM predictable on a 520KB-SRAM ESP32.

| Field | Default | Meaning |
|---|---|---|
| `defaultLimit` | 10 | Listing page size when `ListingOptions.limit` is 0 |
| `maxLimit` | 25 | Hard clamp for listing pages (Reddit allows 100) |
| `minRequestIntervalMs` | 1100 | Client-side pacing between requests |
| `httpTimeoutMs` | 15000 | Per-request timeout, connect + response |
| `maxCommentDepth` | 6 | Max recursion depth when parsing comment replies |
| `tokenRefreshMarginSec` | 300 | Re-authenticate this many seconds before token expiry |
| `insecureTls` | false | Skip TLS certificate validation. Debug only, never ship |
| `rateLimitFloor` | 5 | When `x-ratelimit-remaining` hits this, pause until `x-ratelimit-reset` |

## Transport

`EspTransport` (Arduino only, `#include` via the umbrella header) implements the network layer with `HTTPClient` + `WiFiClientSecure`. Construct once, reuse for every request.

```cpp
reddit::EspTransport transport;                    // default config
reddit::EspTransport transport(myConfig);          // custom config
transport.setCaCert(pem);                          // pin a custom CA (PEM, NUL-terminated), call before first send()
```

TLS trust order: pinned CA via `setCaCert()` first, then the arduino-esp32 built-in Mozilla CA bundle, then skip-validation only if `config.insecureTls` is true. Sync NTP time before the first request or certificate validation fails.

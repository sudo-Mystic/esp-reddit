# Reddit API Reference for ESP32 Client (Arduino, C++)

Dense technical reference for implementing a PRAW-like Reddit API wrapper on ESP32.
Verified against Reddit's official docs (reddit.com/dev/api, reddit-archive/reddit/wiki/OAuth2)
and current third-party references, October 2026.

---

## 1. OAuth2 "Script" App Flow

Script apps are for personal use on hardware you control; the app is locked to the
registering account. Register at https://www.reddit.com/prefs/apps (type: script,
redirect URI `http://localhost:8080` is required but unused for the password grant).

### Token request (password grant)

```
POST https://www.reddit.com/api/v1/access_token
```

Headers (all required):

```
Authorization: Basic base64(client_id:client_secret)
Content-Type:  application/x-www-form-urlencoded
User-Agent:    <platform>:<app_id>:<version> (by /u/<reddit_username>)
```

Body (form-encoded, NOT in URL):

```
grant_type=password&username=<reddit_username>&password=<reddit_password>
```

Notes:
- `client_id` is the ~14-char string shown under the app name; `client_secret` is the
  "secret" field. HTTP Basic = base64 of the literal string `client_id:client_secret`.
- URL-encode the password if it contains reserved characters.
- Accounts with 2FA enabled cannot use the password grant (`invalid_grant`).

### Token response (HTTP 200, JSON)

```json
{
  "access_token": "eyJhbGciOiJSUzI1NiIs...",
  "token_type": "bearer",
  "expires_in": 3600,
  "scope": "*"
}
```

- `expires_in` is seconds (3600 = 1 hour). Re-authenticate before expiry; keep a
  safety margin (e.g. refresh at 300s remaining) since ESP32 clocks can drift.
- Script apps get no refresh token from the password grant by default. The standard
  practice is to simply repeat the password grant when the token expires.
- Scope `*` means all scopes (script apps are granted everything).

### Token errors

| Response | Meaning |
|---|---|
| `{"error": "invalid_grant"}` (200 body) | Wrong username/password, or 2FA enabled |
| HTTP 401 | Invalid client_id/client_secret in Basic auth |
| HTTP 429 | Missing/malformed User-Agent, or rate limited |

### Refresh flow (auth-code apps / permanent tokens)

Per the official OAuth2 wiki, apps that obtained a refresh token (auth-code flow with
`duration=permanent` in the authorization request) renew like this:

```
POST https://www.reddit.com/api/v1/access_token
Authorization: Basic base64(client_id:client_secret)   (same Basic auth as before)
Content-Type:  application/x-www-form-urlencoded

grant_type=refresh_token&refresh_token=<TOKEN>
```

The response has the same shape as the initial token response. For a script app on
ESP32, prefer the simpler pattern: cache `access_token` + fetch time, and re-run the
password grant when expired. Only implement `grant_type=refresh_token` if you add an
auth-code flow.

---

## 2. Authenticated API Base

```
https://oauth.reddit.com
```

Every request must carry:

```
Authorization: Bearer <access_token>
User-Agent:    <platform>:<app_id>:<version> (by /u/<reddit_username>)
```

### User-Agent rules

- Format: `<platform>:<app_id>:<version> (by /u/<username>)`,
  e.g. `esp32:espraw:v1.0.0 (by /u/someuser)`.
- Reddit blocks requests with a missing or default User-Agent. Default library UAs
  (e.g. anything looking like a generic HTTP client) get severely rate-limited
  (HTTP 429).
- Never spoof a browser UA. That gets the client banned, not just limited.
- On ESP32 this string is static per build; embed the firmware version in it so
  Reddit-side abuse reports can be correlated.

### 401 vs 403 semantics

- **401**: bad/expired/missing bearer token (or bad client credentials on the token
  endpoint). Fix: re-authenticate.
- **403**: token is valid but the action is forbidden. Common causes: subreddit is
  private/quarantined/banned, account lacks karma/age for the subreddit's posting
  rules, missing scope (not applicable to script apps with `*`), or hitting a
  moderator-only endpoint.

---

## 3. Endpoints (PRAW-like wrapper surface)

Base for all: `https://oauth.reddit.com`. Pass `api_type=json` on mutating POSTs to
get the `{"json": {...}}` envelope (see section 6). Pass `raw_json=1` on GETs to
disable HTML-entity escaping in text fields.

### Listings

```
GET /r/{sub}/hot
GET /r/{sub}/new
GET /r/{sub}/rising
GET /r/{sub}/controversial
GET /r/{sub}/top
```

Query params: `limit` (default 25, max 100; over-max is clamped, not rejected),
`after` / `before` (fullname anchors for pagination), `count` (number of items
already seen, used with after/before), `t` for top/controversial (`hour`, `day`,
`week`, `month`, `year`, `all`; default `day` for top), `g` geolocation for hot,
`raw_json=1`, `show=all` (include filtered content where permitted).

Front-page equivalents: `/hot`, `/new`, `/rising`, `/controversial`, `/top`.
Multi-reddit: `/r/{sub1}+{sub2}/hot`.

### Submit a post

```
POST /api/submit
```

Form params: `sr` (subreddit name, no `r/`), `kind` (`self` | `link` |
`image` | `video` | `videogif` | `poll`), `title` (max 300 chars),
`text` (markdown body for self/poll) or `url` (for link/image/video),
`resubmit` (true to bypass the duplicate-URL check), `sendreplies` (true/false),
`nsfw` / `spoiler` (true/false), `flair_id` / `flair_text` (if the subreddit uses
flair), `api_type=json`, `extension=json`.

Success: `{"json": {"errors": [], "data": {"id": "<id36>", "name": "t3_<id36>",
"url": "<permalink>"}}}`.

### Vote

```
POST /api/vote
```

Form params: `id` (fullname, e.g. `t3_abc123`), `dir` (`1` upvote, `0` clear,
`-1` downvote). No response body of note on success (HTTP 200).

### Save / unsave

```
POST /api/save     Body: id=<fullname>
POST /api/unsave   Body: id=<fullname>
```

### Comment

```
POST /api/comment
```

Form params: `thing_id` (fullname of parent: `t3_` for top-level on a post,
`t1_` to reply to a comment), `text` (markdown), `api_type=json`.

### Submission comments

```
GET /r/{sub}/comments/{id36}[/{slug}]
```

Returns a JSON **array of two Listings**: `[post_listing, comments_listing]`.
Query params: `sort` (`confidence` | `top` | `new` | `controversial` | `old` |
`qa`; default `confidence`), `depth` (max tree depth), `limit` (max top-level
comments), `comment` (id36 of a comment to focus + `context=N` parents),
`threaded=true`, `raw_json=1`, `showedits`, `showmore`.

### User

```
GET /api/v1/me                        (authenticated account identity)
GET /user/{username}/about            (public profile: karma, created_utc)
GET /user/{username}/overview
GET /user/{username}/submitted        (posts)
GET /user/{username}/comments
```

`overview`/`submitted`/`comments` are Listings and take the standard listing
params (`limit`, `after`, `before`, `sort=new|hot|top|controversial`, `t`).
`upvoted` / `downvoted` / `saved` / `hidden` exist but only return the
authenticated user's own data (require the matching username).

### Subreddit info

```
GET /r/{sub}/about
```

Returns a single Thing (`kind: t5`) with fields like `display_name`,
`subscribers`, `public_description`, `over18`, `submission_type`,
`allow_images`, `accounts_active`.

### Delete / edit

```
POST /api/del           Body: id=<fullname>            (delete own post/comment)
POST /api/editusertext  Body: thing_id=<fullname>&text=<new markdown>
```

### Subscribe / unsubscribe

```
POST /api/subscribe
```

Form params: `action` (`sub` | `unsub`), `sr` (subreddit fullname `t5_xxx`) or
`sr_name` (display name). Both are accepted; `sr_name` is simpler on ESP32.

---

## 4. Thing IDs (Fullnames)

Every Reddit object has a base36 `id` and a `fullname` = `<type prefix>_<id>`.
Mutating endpoints almost always want the fullname.

| Prefix | Type | Example |
|---|---|---|
| `t1_` | Comment | `t1_ab12cd` |
| `t2_` | Account (user) | `t2_1a2b3c` |
| `t3_` | Link / submission (post) | `t3_xyz789` |
| `t4_` | Message | `t4_mno456` |
| `t5_` | Subreddit | `t5_2qh33` |
| `t6_` | Award | (rarely used) |

Rules:
- Listings return both: `child.data.id` (bare) and `child.data.name` (fullname).
- `/api/vote`, `/api/save`, `/api/unsave`, `/api/del` take `id` = fullname.
- `/api/comment` takes `thing_id` = fullname of the parent.
- `/api/editusertext` takes `thing_id` = fullname.
- `/r/{sub}/comments/{id}` in the URL path takes the bare id36.
- Subreddit names in paths are case-insensitive; canonical casing comes back in
  `display_name`. Prefer lowercase in requests.

---

## 5. Rate Limits

Every response carries headers (note: lowercase `x-ratelimit-*` in practice):

```
x-ratelimit-used:      approximate requests used in current window
x-ratelimit-remaining: approximate requests left in the window
x-ratelimit-reset:     approximate seconds until the window resets
```

### Documented limits

- **OAuth (free tier, current policy): 100 queries/minute per OAuth client_id**,
  averaged over a 10-minute window (bursting allowed). Headers are keyed by
  client_id.
- **Historical wiki numbers (still widely cited): 60 req/min, 600 req per
  10 minutes** for OAuth clients. Treat 100 QPM as the ceiling to code against
  and use the `x-ratelimit-*` headers as the live source of truth.
- **Anonymous (no OAuth): ~30 req/min and ~2000 req/day per source IP**, both
  windows must have budget. Much stricter; this is why even read-only ESP32 use
  should prefer a script-app token.
- **Write actions** (submit, comment, vote) are additionally throttled by
  Reddit's spam prevention, independent of HTTP rate limits. A `RATELIMIT` entry
  in the JSON error body (not an HTTP 429) means "wait N minutes/seconds before
  posting again"; the message text states the wait time.

### 429 handling guidance for ESP32

1. On HTTP 429, honor `Retry-After` if present; otherwise back off exponentially
   (e.g. 2s, 4s, 8s... capped at ~5 min) with a small random jitter.
2. Proactive: check `x-ratelimit-remaining` before bursts; pause when it drops
   below ~5.
3. Never retry a 429 in a tight loop on a microcontroller; you will extend the
   ban window and risk the client_id being flagged.
4. Cache aggressively: listings change slowly; a 30-60s cache on the device
   eliminates most accidental limit hits.
5. One OAuth client_id = one budget. If the wrapper is used by many devices with
   the same app credentials, they share the 100 QPM pool.

---

## 6. JSON Response Shapes

### Listing envelope

```json
{
  "kind": "Listing",
  "data": {
    "modhash": "",
    "dist": 25,
    "children": [
      { "kind": "t3", "data": { "...submission fields..." } },
      { "kind": "t1", "data": { "...comment fields..." } }
    ],
    "after": "t3_abc123",
    "before": null
  }
}
```

- `after`/`before` are fullnames used as the next request's pagination cursor.
  `after: null` means end of listing.
- `dist` is the number of children returned.

### Submission (`t3`) data, key fields

`id`, `name` (fullname), `title`, `selftext` (markdown, empty for link posts),
`selftext_html` (null unless requested), `url`, `permalink` (e.g.
`/r/esp32/comments/abc123/slug/`), `author`, `subreddit`, `subreddit_id` (t5_),
`score`, `ups`, `downs` (usually 0, fuzzed/hidden), `num_comments`,
`created_utc` (Unix seconds), `over_18`, `spoiler`, `stickied`, `locked`,
`link_flair_text`, `link_flair_type`, `is_self`, `is_video`, `is_original_content`,
`distinguished`, `edited` (false or Unix timestamp), `saved`, `archived`,
`thumbnail`, `domain`, `upvote_ratio`, `total_awards_received`.

### Comment (`t1`) data, key fields

`id`, `name`, `body` (markdown), `body_html`, `author`, `subreddit`,
`link_id` (parent submission fullname), `parent_id` (direct parent fullname),
`score`, `created_utc`, `depth`, `distinguished`, `edited`, `stickied`,
`score_hidden`, `saved`, `controversiality`.

**`replies` is polymorphic**: it is the empty string `""` when there are no
replies, otherwise a nested Listing object `{"kind": "Listing", "data":
{"children": [...]}}`. The parser must check the JSON type before recursing.
With `depth`/`limit` params, deep threads are replaced by `"more"` children:
`{"kind": "more", "data": {"children": ["<id36>", ...], "count": N,
"parent_id": "t1_..."}}`. Resolving them needs `POST /api/morechildren`
(`link_id`, comma-separated `children`, `api_type=json`).

### Error envelope (mutating endpoints, with `api_type=json`)

Reddit returns a jQuery-style envelope, HTTP 200 even on application errors:

```json
{ "json": { "errors": [ ["RATELIMIT", "you are doing that too much. try again in 8 minutes.", "ratelimit"] ], "data": {} } }
```

- Each error is `[code, human_message, field]`. Empty `errors: []` = success.
- Common codes: `RATELIMIT` (spam cooldown, message states the wait),
  `NO_TEXT` (missing required field), `BAD_SR_NAME`, `SUBREDDIT_NOEXIST`,
  `USER_REQUIRED`, `NO_SUBJECT`/`NO_TEXT` variants.
- **Always check `json.errors` even on HTTP 200.** A 200 with non-empty errors
  is a failed mutation.

---

## 7. Read-Only (No-Auth) Access

Public endpoints work without OAuth via `https://www.reddit.com`:

```
GET https://www.reddit.com/r/{sub}/hot/.json
GET https://www.reddit.com/r/{sub}/comments/{id}/.json
GET https://www.reddit.com/user/{name}/submitted/.json
GET https://www.reddit.com/r/{sub}/about/.json
```

Rules:
- Append `.json` (or use `?format=json`); `raw_json=1` still applies.
- A proper `User-Agent` is **mandatory**; missing/default UA is blocked or
  429'd immediately.
- Limits are stricter: ~30 req/min and ~2000 req/day per source IP (see
  section 5). Behind carrier NAT the budget is shared with strangers.
- Anything requiring identity or mutation (vote, save, submit, comment,
  `/api/v1/me`, private/quarantined subreddits, `upvoted`/`saved` listings)
  needs OAuth. No exceptions.
- old.reddit.com `/.json` and `/.rss` variants exist but are legacy; prefer
  www.reddit.com.

---

## 8. Gotchas

1. **User-Agent is the #1 first-run failure.** A correct token flow followed by
   429s on every API call almost always means the UA is missing, generic, or
   malformed. The token endpoint itself also wants the UA.
2. **Wrong base host.** Authenticated calls go to `https://oauth.reddit.com`.
   Using `www.reddit.com` with a bearer token returns 401/403 or redirects.
   The token endpoint is the exception: it lives on `www.reddit.com`.
3. **401 vs 403**: 401 = token bad/expired (re-authenticate); 403 = action
   forbidden (private sub, karma/age gate, wrong scope). Do not re-auth on 403.
4. **Token expiry is 1 hour.** Cache `access_token` + `obtained_at`; re-run the
   password grant at ~55 minutes. ESP32 has no RTC guarantee, so track elapsed
   time with `millis()` from the moment of issuance.
5. **`/api/v1/access_token` returns 401 on a bad client secret**, and
   `{"error": "invalid_grant"}` (HTTP 200) on bad username/password or 2FA.
   Handle both; they need different user-facing messages.
6. **Subreddit names are case-insensitive** in URLs but canonical casing is in
   `display_name`. Normalize to lowercase for cache keys.
7. **Mutations return HTTP 200 on failure.** Always parse `json.errors`; never
   trust the status code alone for POSTs.
8. **`replies: ""` vs object.** The polymorphic replies field crashes naive
   parsers. Check type first.
9. **`limit` over 100 is clamped, not rejected.** Do not treat a short page as
   an error; check `after == null` for end-of-listing.
10. **RATELIMIT (JSON body) != HTTP 429.** The former is per-account spam
    prevention with a human-readable wait time; parse the minutes/seconds out
    of the message and sleep that long.
11. **Write endpoints need form-encoded bodies**, not JSON. `Content-Type:
    application/x-www-form-urlencoded` on all POSTs.
12. **Keep secrets off the device log.** Never print `client_secret`, password,
    or tokens to Serial in production builds; a leaked token in a pasted log
    is a compromised account.
13. **One client_id, shared budget.** All devices using the same script app
    share the 100 QPM pool. Design polling intervals accordingly.
14. **Reddit API terms changed in 2023.** High-volume or commercial use needs a
    paid tier; personal low-volume script use stays in the free tier, but limits
    and terms can change. If normal usage suddenly 403/429s, check
    https://www.reddit.com/wiki/api/ before debugging code.

---

## Sources

- https://github.com/reddit-archive/reddit/wiki/OAuth2 (token + refresh flow)
- https://www.reddit.com/dev/api (endpoint reference)
- https://github.com/reddit-archive/reddit/wiki/API#rules (rate limit rules)
- https://github.com/nitaiaharoni1/reddit-mcp/blob/HEAD/reddit.api.md (current 100 QPM policy, header names)
- https://github.com/modcommunity/website-learn/blob/HEAD/src/content/docs/en/api/rate-limits.mdx (anonymous read limits)
- https://github.com/gregrickaby/viewer-for-reddit/blob/HEAD/.agents/instructions/reddit-api.instructions.md (listing/comment params, response shapes)
- https://github.com/chancehouston/atwfan/blob/HEAD/ATWFanBot/REDDIT_API_GETTING_STARTED.md (script flow, error table, submit params)
- https://github.com/joeysmithjrs/hermes-agent/blob/HEAD/skills/social-media/reddit-api/SKILL.md (troubleshooting, RATELIMIT vs 429)

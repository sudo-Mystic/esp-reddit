#include "RedditModels.h"

#include <ArduinoJson.h>

namespace reddit {

namespace {

// Whitelist for parseSubmissionListing: with the filter active, a 100KB
// listing collapses to a few KB in the document.
void buildSubmissionFilter(JsonDocument& filter) {
  static const char* const kFields[] = {
      "id",           "name",        "title",     "selftext",  "url",
      "permalink",    "author",      "subreddit", "subreddit_id",
      "score",        "num_comments", "created_utc", "upvote_ratio",
      "over_18",      "spoiler",     "stickied",  "locked",    "is_self",
      "saved",        "archived",    "link_flair_text",
  };
  JsonObject data = filter["data"].to<JsonObject>();
  // Index 0 in a filter applies the sub-filter to every array element.
  JsonObject childData = data["children"][0]["data"].to<JsonObject>();
  for (const char* f : kFields) childData[f] = true;
  data["after"] = true;
}

// Safe descent helpers: never crash on missing/m mistyped nodes.
JsonObject childObject(JsonObject obj, const char* key) {
  if (obj.isNull()) return JsonObject();
  return obj[key].as<JsonObject>();
}

JsonArray childArray(JsonObject obj, const char* key) {
  if (obj.isNull()) return JsonArray();
  return obj[key].as<JsonArray>();
}

// Ensure a "<prefix>_<id36>" fullname. Reddit is inconsistent: some
// endpoints return the bare id36 in "id", others the fullname (or put the
// fullname in "name"). The structs document fullnames, so normalize.
std::string withPrefix(const std::string& v, const char* prefix) {
  if (v.empty()) return v;
  if (v.rfind(prefix, 0) == 0) return v;
  return std::string(prefix) + v;
}

void fillSubmission(JsonObject d, Submission& s) {
  s.id = d["id"] | "";
  // Prefer "name" (fullname); some shapes only carry the bare id.
  const std::string name = d["name"] | "";
  s.fullname = name.empty() ? withPrefix(s.id, "t3_") : name;
  s.title = d["title"] | "";
  s.selftext = d["selftext"] | "";
  s.url = d["url"] | "";
  s.permalink = d["permalink"] | "";
  s.author = d["author"] | "";
  s.subreddit = d["subreddit"] | "";
  s.subredditId = d["subreddit_id"] | "";
  s.flairText = d["link_flair_text"] | "";
  s.score = d["score"] | 0L;
  s.numComments = d["num_comments"] | 0L;
  s.createdUtc = d["created_utc"] | 0.0;
  s.upvoteRatio = d["upvote_ratio"] | 0.0;
  s.over18 = d["over_18"] | false;
  s.spoiler = d["spoiler"] | false;
  s.stickied = d["stickied"] | false;
  s.locked = d["locked"] | false;
  s.isSelf = d["is_self"] | false;
  s.saved = d["saved"] | false;
  s.archived = d["archived"] | false;
}

void fillComment(JsonObject d, Comment& c, int depth) {
  c.id = d["id"] | "";
  const std::string name = d["name"] | "";
  c.fullname = name.empty() ? withPrefix(c.id, "t1_") : name;
  c.body = d["body"] | "";
  c.author = d["author"] | "";
  c.subreddit = d["subreddit"] | "";
  c.linkId = d["link_id"] | "";
  c.parentId = d["parent_id"] | "";
  c.score = d["score"] | 0L;
  c.createdUtc = d["created_utc"] | 0.0;
  c.depth = depth;
  c.scoreHidden = d["score_hidden"] | false;
  c.stickied = d["stickied"] | false;
}

void parseCommentChildren(JsonArray children, std::vector<Comment>& out,
                          int depth, uint8_t maxDepth) {
  if (children.isNull()) return;
  for (JsonVariant child : children) {
    if (!child.is<JsonObject>()) continue;
    // Skip "more" placeholders and anything that isn't a comment.
    const std::string kind = child["kind"] | "";
    if (kind != "t1") continue;
    JsonObject d = child["data"].as<JsonObject>();
    if (d.isNull()) continue;
    Comment c;
    fillComment(d, c, depth);
    // "replies" is polymorphic: "" when empty, a nested Listing otherwise.
    JsonVariant replies = d["replies"];
    if (depth < static_cast<int>(maxDepth) && replies.is<JsonObject>()) {
      parseCommentChildren(childArray(childObject(replies, "data"), "children"),
                           c.replies, depth + 1, maxDepth);
    }
    out.push_back(std::move(c));
  }
}

}  // namespace

bool parseSubmissionListing(const std::string& json,
                            std::vector<Submission>& out, std::string& after,
                            uint8_t maxItems) {
  out.clear();
  after.clear();

  JsonDocument filter;
  buildSubmissionFilter(filter);

  JsonDocument doc;
  const DeserializationError err = deserializeJson(
      doc, json.c_str(), json.size(), DeserializationOption::Filter(filter));
  if (err) return false;

  const JsonObject data = doc["data"].as<JsonObject>();
  if (data.isNull()) return false;

  const JsonArray children = data["children"].as<JsonArray>();
  if (!children.isNull()) {
    for (JsonObject child : children) {
      if (out.size() >= maxItems) break;
      const JsonObject d = child["data"].as<JsonObject>();
      if (d.isNull()) continue;
      Submission s;
      fillSubmission(d, s);
      out.push_back(std::move(s));
    }
  }
  after = data["after"] | "";
  return true;
}

bool parseCommentsResponse(const std::string& json, Submission& post,
                           std::vector<Comment>& comments, uint8_t maxDepth) {
  post = Submission{};
  comments.clear();

  JsonDocument doc;  // comments need recursive fields; filters can't express
                     // unbounded nesting, so parse whole but depth-cap.
  // One reply level costs ~5 nesting levels
  // ([{data{children[{data{replies{data{children[...
  // so allow headroom well past maxCommentDepth (default 6 -> ~36).
  constexpr unsigned kCommentNestingLimit = 40;
  if (deserializeJson(doc, json.c_str(), json.size(),
                      DeserializationOption::NestingLimit(
                          kCommentNestingLimit)))
    return false;

  const JsonArray arr = doc.as<JsonArray>();
  if (arr.isNull() || arr.size() < 2) return false;

  // Element 0: the post listing; take its first child.
  const JsonObject postListing = arr[0].as<JsonObject>();
  const JsonArray postChildren =
      childArray(childObject(postListing, "data"), "children");
  if (!postChildren.isNull() && postChildren.size() > 0) {
    const JsonObject pd =
        postChildren[0]["data"].as<JsonObject>();
    if (!pd.isNull()) fillSubmission(pd, post);
  }

  // Element 1: the comments listing.
  const JsonObject commentsListing = arr[1].as<JsonObject>();
  parseCommentChildren(
      childArray(childObject(commentsListing, "data"), "children"), comments,
      0, maxDepth);
  return true;
}

bool parseRedditor(const std::string& json, Redditor& out) {
  out = Redditor{};

  JsonDocument doc;  // tiny response: no filter needed
  if (deserializeJson(doc, json.c_str(), json.size())) return false;

  const JsonObject root = doc.as<JsonObject>();
  if (root.isNull()) return false;

  // /user/{name}/about -> {"kind":"t2","data":{...}};
  // /api/v1/me        -> flat object (no kind/data envelope).
  const std::string kind = root["kind"] | "";
  const JsonObject d =
      (kind == "t2") ? childObject(root, "data") : root;
  if (d.isNull()) return false;

  out.id = withPrefix(d["id"] | "", "t2_");
  out.name = d["name"] | "";
  out.linkKarma = d["link_karma"] | 0L;
  out.commentKarma = d["comment_karma"] | 0L;
  out.createdUtc = d["created_utc"] | 0.0;
  out.isGold = d["is_gold"] | false;
  out.isMod = d["is_mod"] | false;
  return true;
}

bool parseSubredditInfo(const std::string& json, SubredditInfo& out) {
  out = SubredditInfo{};

  JsonDocument doc;  // tiny response: no filter needed
  if (deserializeJson(doc, json.c_str(), json.size())) return false;

  const JsonObject root = doc.as<JsonObject>();
  if (root.isNull()) return false;

  const JsonObject d = childObject(root, "data");
  if (d.isNull()) return false;

  // Prefer "name" (fullname on Things); fall back to "id".
  const std::string name = d["name"] | "";
  out.id = name.empty() ? withPrefix(d["id"] | "", "t5_") : name;
  out.displayName = d["display_name"] | "";
  out.title = d["title"] | "";
  out.publicDescription = d["public_description"] | "";
  out.submissionType = d["submission_type"] | "";
  out.subscribers = d["subscribers"] | 0L;
  out.activeCount = d["accounts_active"] | 0L;
  out.over18 = d["over18"] | false;
  return true;
}

bool parseMutationResult(const std::string& json, std::string& errCode,
                         std::string& errMessage, std::string& outId) {
  errCode.clear();
  errMessage.clear();
  outId.clear();

  JsonDocument doc;  // tiny response: no filter needed
  if (deserializeJson(doc, json.c_str(), json.size())) return false;

  const JsonObject envelope = childObject(doc.as<JsonObject>(), "json");
  if (envelope.isNull()) return false;

  const JsonArray errors = envelope["errors"].as<JsonArray>();
  if (!errors.isNull() && errors.size() > 0) {
    const JsonArray first = errors[0].as<JsonArray>();
    if (!first.isNull()) {
      errCode = first[0] | "";
      errMessage = first[1] | "";
    }
    return false;
  }

  const JsonObject data = childObject(envelope, "data");
  if (!data.isNull()) {
    // data.name when present (fullname), else data.id; absent -> empty.
    const char* nm = data["name"] | "";
    outId = (nm[0] != '\0') ? nm : (data["id"] | "");
  }
  return true;
}

}  // namespace reddit

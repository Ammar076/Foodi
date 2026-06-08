#include "services/OffClient.h"

#include <curl/curl.h>
#include <drogon/drogon.h>  // app().getCustomConfig(), LOG_* macros
#include <json/json.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <set>

using namespace drogon;

namespace {

std::string trimWs(const std::string &s)
{
    size_t b = s.find_first_not_of(" \t");
    if (b == std::string::npos)
        return "";
    size_t e = s.find_last_not_of(" \t");
    return s.substr(b, e - b + 1);
}

// First entry of a comma-separated list (OFF "brands" can hold several).
std::string firstCsv(const std::string &s)
{
    auto pos = s.find(',');
    return trimWs(pos == std::string::npos ? s : s.substr(0, pos));
}

// Normalized "name|brand" key for de-duplication: lowercased, whitespace collapsed.
// Lets us collapse the same product re-listed per size/country into one entry.
std::string normKey(const std::string &name, const std::string &brand)
{
    auto norm = [](const std::string &s) {
        std::string out;
        bool pendingSpace = false;
        for (unsigned char c : s) {
            if (std::isspace(c)) {
                pendingSpace = !out.empty();
            } else {
                if (pendingSpace) {
                    out.push_back(' ');
                    pendingSpace = false;
                }
                out.push_back(static_cast<char>(std::tolower(c)));
            }
        }
        return out;
    };
    return norm(name) + "|" + norm(brand);
}

// "en:sweet-snacks" -> "Sweet snacks"
std::string readableTag(const std::string &tag)
{
    if (tag.empty())
        return "";
    std::string t = tag;
    auto pos = t.find(':');
    if (pos != std::string::npos)
        t = t.substr(pos + 1);
    std::replace(t.begin(), t.end(), '-', ' ');
    if (!t.empty())
        t[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(t[0])));
    return t;
}

std::string firstTag(const Json::Value &arr)
{
    if (arr.isArray() && !arr.empty())
        return arr[0].asString();
    return "";
}

std::string dietFrom(const Json::Value &arr)
{
    if (!arr.isArray())
        return "";
    bool vegan = false, vegetarian = false;
    for (const auto &v : arr) {
        auto s = v.asString();
        if (s == "en:vegan")
            vegan = true;
        else if (s == "en:vegetarian")
            vegetarian = true;
    }
    if (vegan)
        return "vegan";
    if (vegetarian)
        return "vegetarian";
    return "";
}

std::vector<std::string> toStringVec(const Json::Value &arr)
{
    std::vector<std::string> v;
    if (arr.isArray())
        for (const auto &e : arr)
            v.push_back(e.asString());
    return v;
}

// libcurl write callback: append received bytes to a std::string.
size_t appendToString(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *body = static_cast<std::string *>(userdata);
    body->append(ptr, size * nmemb);
    return size * nmemb;
}

}  // namespace

namespace foodi {

OffClient &OffClient::instance()
{
    static OffClient inst;
    return inst;
}

OffClient::OffClient()
{
    // Process-wide libcurl init. Safe to call once here: the singleton is created
    // via a function-local static, so this constructor runs exactly once.
    curl_global_init(CURL_GLOBAL_DEFAULT);

    const auto &cfg = app().getCustomConfig();
    baseUrl_ = cfg.get("off_base_url", "https://uk.openfoodfacts.org").asString();
    userAgent_ = cfg.get("off_user_agent", "Foodi/0.1").asString();
}

std::vector<OffProduct> OffClient::search(const std::string &query, int limit)
{
    std::vector<OffProduct> out;

    // One easy handle per call keeps this thread-safe across request handlers.
    CURL *curl = curl_easy_init();
    if (!curl) {
        LOG_ERROR << "curl_easy_init failed";
        return out;
    }

    // URL-encode the only user-controlled field; the rest are fixed literals.
    char *escaped = curl_easy_escape(curl, query.c_str(), static_cast<int>(query.size()));
    std::string terms = escaped ? escaped : "";
    if (escaped)
        curl_free(escaped);

    // English results, most-scanned (mainstream) first. The country endpoint
    // (off_base_url, e.g. uk.openfoodfacts.org) is what actually yields English-
    // primary products — the world DB is EU-centric. We over-fetch because the
    // English filter + de-duplication below trim the list.
    const int fetch = std::min(limit * 2, 50);
    std::string url = baseUrl_ + "/cgi/search.pl?search_terms=" + terms +
                      "&search_simple=1&action=process&json=1"
                      "&sort_by=unique_scans_n&lc=en&page_size=" +
                      std::to_string(fetch) +
                      "&fields=code,product_name,lang,brands,allergens_tags,traces_tags,"
                      "ingredients_text,ingredients_analysis_tags,categories_tags,"
                      "image_url,image_front_small_url";

    std::string body;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, userAgent_.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);  // OFF search.pl may 301
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");  // accept gzip/deflate
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

    CURLcode rc = curl_easy_perform(curl);
    long httpStatus = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpStatus);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
        LOG_WARN << "OFF request failed: " << curl_easy_strerror(rc);
        return out;
    }
    if (httpStatus != 200) {
        LOG_WARN << "OFF returned HTTP " << httpStatus;
        return out;
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errs;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    if (!reader->parse(body.data(), body.data() + body.size(), &root, &errs)) {
        LOG_WARN << "OFF JSON parse error: " << errs;
        return out;
    }

    const auto &products = root["products"];
    if (!products.isArray())
        return out;

    std::set<std::string> seen;  // normalized name|brand -> de-dup
    for (const auto &p : products) {
        OffProduct prod;
        prod.barcode = p.get("code", "").asString();
        prod.name = p.get("product_name", "").asString();
        if (prod.barcode.empty() || prod.name.empty())
            continue;  // unusable without a key + name

        // English only: drop products whose primary language isn't English (a few
        // slip through even on the country endpoint).
        const std::string lang = p.get("lang", "").asString();
        if (!lang.empty() && lang != "en")
            continue;

        prod.brand = firstCsv(p.get("brands", "").asString());

        // Collapse the same product re-listed per size/country; OFF is popularity-
        // sorted, so the first (kept) one is the most-scanned.
        if (!seen.insert(normKey(prod.name, prod.brand)).second)
            continue;
        prod.ingredients_text = p.get("ingredients_text", "").asString();
        prod.image_url = p.get("image_url", "").asString();
        if (prod.image_url.empty())
            prod.image_url = p.get("image_front_small_url", "").asString();
        prod.category = readableTag(firstTag(p["categories_tags"]));
        prod.diet = dietFrom(p["ingredients_analysis_tags"]);
        prod.allergen_tags = toStringVec(p["allergens_tags"]);
        prod.trace_tags = toStringVec(p["traces_tags"]);

        out.push_back(std::move(prod));
    }
    return out;
}

}  // namespace foodi

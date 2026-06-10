#include "services/SpoonacularClient.h"

#include <curl/curl.h>
#include <drogon/drogon.h>  // app().getCustomConfig(), LOG_* macros
#include <json/json.h>

#include <cctype>
#include <cstdlib>
#include <memory>
#include <string>

using namespace drogon;

namespace {

// Secrets come from the environment in production; config.json is the dev fallback.
std::string envOr(const char *key, const std::string &fallback)
{
    const char *v = std::getenv(key);
    return (v && *v) ? std::string(v) : fallback;
}

// "main course" -> "Main course"
std::string capitalize(const std::string &s)
{
    if (s.empty())
        return s;
    std::string out = s;
    out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
    return out;
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

SpoonacularClient &SpoonacularClient::instance()
{
    static SpoonacularClient inst;
    return inst;
}

SpoonacularClient::SpoonacularClient()
{
    const auto &cfg = app().getCustomConfig();
    apiKey_ = envOr("FOODI_SPOONACULAR_KEY", cfg.get("spoonacular_api_key", "").asString());
    // Treat the example placeholder as "no key".
    if (apiKey_ == "FILL_ME" || apiKey_ == "PASTE_YOUR_SPOONACULAR_KEY_HERE")
        apiKey_.clear();
    baseUrl_ = cfg.get("spoonacular_base_url", "https://api.spoonacular.com").asString();
}

std::vector<SpoonacularRecipe> SpoonacularClient::search(const std::string &query, int number)
{
    std::vector<SpoonacularRecipe> out;
    if (apiKey_.empty()) {
        LOG_WARN << "Spoonacular search skipped: no API key configured";
        return out;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        LOG_ERROR << "curl_easy_init failed";
        return out;
    }

    // URL-encode the only user-controlled field; the key/params are fixed.
    char *escaped = curl_easy_escape(curl, query.c_str(), static_cast<int>(query.size()));
    std::string terms = escaped ? escaped : "";
    if (escaped)
        curl_free(escaped);

    const int n = std::min(std::max(number, 1), 50);
    // addRecipeInformation -> diets/dishTypes/flags; fillIngredients -> ingredient list.
    std::string url = baseUrl_ + "/recipes/complexSearch?query=" + terms +
                      "&number=" + std::to_string(n) +
                      "&addRecipeInformation=true&fillIngredients=true&sort=popularity"
                      "&apiKey=" + apiKey_;

    std::string body;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");  // accept gzip/deflate
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

    CURLcode rc = curl_easy_perform(curl);
    long httpStatus = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpStatus);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
        LOG_WARN << "Spoonacular request failed: " << curl_easy_strerror(rc);
        return out;
    }
    if (httpStatus != 200) {
        // 401 = bad key, 402 = daily quota exhausted — both worth a clear log line.
        LOG_WARN << "Spoonacular returned HTTP " << httpStatus;
        return out;
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errs;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    if (!reader->parse(body.data(), body.data() + body.size(), &root, &errs)) {
        LOG_WARN << "Spoonacular JSON parse error: " << errs;
        return out;
    }

    const auto &results = root["results"];
    if (!results.isArray())
        return out;

    for (const auto &r : results) {
        SpoonacularRecipe rec;
        rec.spoonacular_id = r.get("id", 0).asInt();
        rec.name = r.get("title", "").asString();
        if (rec.spoonacular_id == 0 || rec.name.empty())
            continue;

        rec.brand = r.get("sourceName", "").asString();
        rec.image_url = r.get("image", "").asString();

        if (r.isMember("dishTypes") && r["dishTypes"].isArray() && !r["dishTypes"].empty())
            rec.category = capitalize(r["dishTypes"][0].asString());
        else
            rec.category = "Recipe";

        if (r.get("vegan", false).asBool())
            rec.diet = "vegan";
        else if (r.get("vegetarian", false).asBool())
            rec.diet = "vegetarian";

        // Join ingredient lines into one text blob; the ingest synonym matcher
        // tokenizes it to detect allergens (recipes carry no structured tags).
        std::string ing;
        if (r.isMember("extendedIngredients") && r["extendedIngredients"].isArray()) {
            for (const auto &e : r["extendedIngredients"]) {
                std::string line = e.get("original", "").asString();
                if (line.empty())
                    line = e.get("name", "").asString();
                if (line.empty())
                    continue;
                if (!ing.empty())
                    ing += ", ";
                ing += line;
            }
        }
        rec.ingredients_text = ing;

        out.push_back(std::move(rec));
    }
    return out;
}

}  // namespace foodi

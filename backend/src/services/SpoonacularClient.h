#pragma once
#include <string>
#include <vector>

namespace foodi {

// A recipe fetched from Spoonacular, normalized to what we cache. Recipes have no
// barcode and no structured allergen tags — allergens are resolved at ingestion by
// scanning the ingredient text with the synonym dictionary (best-effort, the same
// fallback path groceries use when OFF tags are sparse).
struct SpoonacularRecipe {
    int spoonacular_id = 0;
    std::string name;
    std::string brand;             // source attribution (e.g. the recipe's site)
    std::string category;          // first dish type, made readable ("Main course")
    std::string diet;              // "vegan" / "vegetarian" / "" (from Spoonacular flags)
    std::string ingredients_text;  // joined ingredient list, for synonym matching
    std::string image_url;
};

// Talks to Spoonacular over libcurl (same rationale as OffClient: OS network/TLS
// stack, synchronous, fits the cache-miss path). Disabled when no API key is set.
class SpoonacularClient
{
public:
    static SpoonacularClient &instance();

    // True when an API key is configured; callers should skip recipe fetches otherwise.
    bool enabled() const { return !apiKey_.empty(); }

    // Search recipes by free text. `number` caps how many to pull (Spoonacular's
    // free tier is ~150 requests/day, so we fetch a generous pool per query and
    // page over the cache rather than calling per page).
    std::vector<SpoonacularRecipe> search(const std::string &query, int number);

private:
    SpoonacularClient();
    SpoonacularClient(const SpoonacularClient &) = delete;
    SpoonacularClient &operator=(const SpoonacularClient &) = delete;

    std::string apiKey_;
    std::string baseUrl_;
};

}  // namespace foodi

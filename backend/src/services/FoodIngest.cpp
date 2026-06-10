#include "services/FoodIngest.h"

#include <drogon/drogon.h>
#include <drogon/orm/Exception.h>

#include <cctype>
#include <set>
#include <string>
#include <unordered_map>

using namespace drogon;

namespace {

// Lowercase alphanumeric tokens from free text (for synonym matching).
std::vector<std::string> tokenize(const std::string &text)
{
    std::vector<std::string> toks;
    std::string cur;
    for (char ch : text) {
        unsigned char c = static_cast<unsigned char>(ch);
        if (std::isalnum(c)) {
            cur.push_back(static_cast<char>(std::tolower(c)));
        } else if (!cur.empty()) {
            toks.push_back(cur);
            cur.clear();
        }
    }
    if (!cur.empty())
        toks.push_back(cur);
    return toks;
}

// Gluten-free flour/starch sources: when "flour" is qualified by one of these
// (e.g. "almond flour", "rice flour"), it does NOT imply gluten.
const std::set<std::string> kGlutenFreeFlour = {
    "almond", "coconut", "rice", "corn", "cornmeal", "maize", "chickpea", "gram",
    "buckwheat", "tapioca", "potato", "soy", "soya", "cassava", "plantain",
    "quinoa", "millet", "sorghum", "oat", "oatmeal"};

// High-confidence wheat-based words. ("flour" is handled separately because it
// needs the gluten-free-qualifier check above.)
const std::set<std::string> kGlutenWords = {
    "dough", "breadcrumb", "breadcrumbs", "pasta", "spaghetti", "macaroni",
    "pastry", "croutons"};

// Resolve allergen ids from free ingredient text: exact synonym-token matches plus
// a gluten heuristic for "flour"/"dough"/etc. that the flat synonym list misses.
// Used for recipes (which carry no structured tags) and the grocery text fallback.
std::set<int> resolveFromText(const std::string &text,
                              const std::unordered_map<std::string, int> &synMap)
{
    std::set<int> out;
    auto toks = tokenize(text);
    for (const auto &t : toks) {
        auto it = synMap.find(t);
        if (it != synMap.end())
            out.insert(it->second);
    }
    // "wheat" is a seeded synonym of the gluten allergen, so its id tells us which
    // allergen to add when we spot an un-qualified flour or a wheat-based word.
    auto g = synMap.find("wheat");
    if (g != synMap.end()) {
        const int glutenId = g->second;
        for (size_t i = 0; i < toks.size(); ++i) {
            if (toks[i] == "flour") {
                const bool gf = (i > 0 && kGlutenFreeFlour.count(toks[i - 1]) > 0);
                if (!gf)
                    out.insert(glutenId);
            } else if (kGlutenWords.count(toks[i]) > 0) {
                out.insert(glutenId);
            }
        }
    }
    return out;
}

}  // namespace

namespace foodi::ingest {

int ingest(const std::vector<OffProduct> &products)
{
    auto db = app().getDbClient();

    // off_tag -> allergen id, and synonym term -> allergen id
    std::unordered_map<std::string, int> offMap;
    for (const auto &row : db->execSqlSync("SELECT id, off_tag FROM allergens"))
        offMap[row["off_tag"].as<std::string>()] = row["id"].as<int>();

    std::unordered_map<std::string, int> synMap;
    for (const auto &row : db->execSqlSync("SELECT allergen_id, term FROM allergen_synonyms"))
        synMap[row["term"].as<std::string>()] = row["allergen_id"].as<int>();

    int count = 0;
    for (const auto &p : products) {
        std::set<int> contains, traces;

        for (const auto &t : p.allergen_tags) {
            auto it = offMap.find(t);
            if (it != offMap.end())
                contains.insert(it->second);
        }
        for (const auto &t : p.trace_tags) {
            auto it = offMap.find(t);
            if (it != offMap.end())
                traces.insert(it->second);
        }
        // Fallback: only scan ingredient text when OFF gave us no allergen tags.
        if (contains.empty() && !p.ingredients_text.empty()) {
            auto fromText = resolveFromText(p.ingredients_text, synMap);
            contains.insert(fromText.begin(), fromText.end());
        }

        try {
            auto trans = db->newTransaction();
            auto r = trans->execSqlSync(
                "INSERT INTO food_items "
                "(off_barcode, name, brand, category, diet, ingredients_text, image_url, last_fetched_at) "
                "VALUES ($1, $2, NULLIF($3,''), NULLIF($4,''), NULLIF($5,''), NULLIF($6,''), NULLIF($7,''), now()) "
                "ON CONFLICT (off_barcode) DO UPDATE SET "
                "  name=EXCLUDED.name, brand=EXCLUDED.brand, category=EXCLUDED.category, "
                "  diet=EXCLUDED.diet, ingredients_text=EXCLUDED.ingredients_text, "
                "  image_url=EXCLUDED.image_url, last_fetched_at=now() "
                "RETURNING id",
                p.barcode, p.name, p.brand, p.category, p.diet, p.ingredients_text, p.image_url);

            int fid = r[0]["id"].as<int>();
            trans->execSqlSync("DELETE FROM food_contains WHERE food_id = $1", fid);
            trans->execSqlSync("DELETE FROM food_traces WHERE food_id = $1", fid);
            for (int aid : contains)
                trans->execSqlSync(
                    "INSERT INTO food_contains (food_id, allergen_id) VALUES ($1, $2) "
                    "ON CONFLICT DO NOTHING",
                    fid, aid);
            for (int aid : traces)
                trans->execSqlSync(
                    "INSERT INTO food_traces (food_id, allergen_id) VALUES ($1, $2) "
                    "ON CONFLICT DO NOTHING",
                    fid, aid);
            ++count;
        } catch (const orm::DrogonDbException &e) {
            LOG_WARN << "ingest failed for barcode " << p.barcode << ": " << e.base().what();
        }
    }
    return count;
}

int ingestRecipes(const std::vector<SpoonacularRecipe> &recipes)
{
    auto db = app().getDbClient();

    // Recipes have no structured tags, so only the synonym dictionary applies.
    std::unordered_map<std::string, int> synMap;
    for (const auto &row : db->execSqlSync("SELECT allergen_id, term FROM allergen_synonyms"))
        synMap[row["term"].as<std::string>()] = row["allergen_id"].as<int>();

    int count = 0;
    for (const auto &r : recipes) {
        std::set<int> contains = resolveFromText(r.ingredients_text, synMap);

        try {
            auto trans = db->newTransaction();
            auto res = trans->execSqlSync(
                "INSERT INTO food_items "
                "(spoonacular_id, kind, name, brand, category, diet, ingredients_text, image_url, last_fetched_at) "
                "VALUES ($1, 'recipe', $2, NULLIF($3,''), NULLIF($4,''), NULLIF($5,''), NULLIF($6,''), NULLIF($7,''), now()) "
                // The predicate must be repeated so ON CONFLICT can infer the
                // partial unique index idx_food_items_spoonacular.
                "ON CONFLICT (spoonacular_id) WHERE spoonacular_id IS NOT NULL DO UPDATE SET "
                "  name=EXCLUDED.name, brand=EXCLUDED.brand, category=EXCLUDED.category, "
                "  diet=EXCLUDED.diet, ingredients_text=EXCLUDED.ingredients_text, "
                "  image_url=EXCLUDED.image_url, last_fetched_at=now() "
                "RETURNING id",
                r.spoonacular_id, r.name, r.brand, r.category, r.diet, r.ingredients_text,
                r.image_url);

            int fid = res[0]["id"].as<int>();
            trans->execSqlSync("DELETE FROM food_contains WHERE food_id = $1", fid);
            // No traces: recipes have no "may contain" data.
            for (int aid : contains)
                trans->execSqlSync(
                    "INSERT INTO food_contains (food_id, allergen_id) VALUES ($1, $2) "
                    "ON CONFLICT DO NOTHING",
                    fid, aid);
            ++count;
        } catch (const orm::DrogonDbException &e) {
            LOG_WARN << "recipe ingest failed for id " << r.spoonacular_id << ": "
                     << e.base().what();
        }
    }
    return count;
}

}  // namespace foodi::ingest

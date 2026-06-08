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
            for (const auto &tok : tokenize(p.ingredients_text)) {
                auto it = synMap.find(tok);
                if (it != synMap.end())
                    contains.insert(it->second);
            }
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

}  // namespace foodi::ingest

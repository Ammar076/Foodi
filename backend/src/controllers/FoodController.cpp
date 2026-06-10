#include "controllers/FoodController.h"

#include <drogon/drogon.h>
#include <drogon/orm/Exception.h>

#include <algorithm>
#include <cstdint>

#include "services/FoodIngest.h"
#include "services/OffClient.h"
#include "services/SpoonacularClient.h"
#include "util/Responses.h"

using namespace drogon;
using namespace foodi;

namespace {

std::string fieldStr(const orm::Row &row, const char *col)
{
    return row[col].isNull() ? std::string() : row[col].as<std::string>();
}

// "Milk|Peanuts" -> JSON ["Milk","Peanuts"]; "" -> []
Json::Value pipeToArray(const std::string &joined)
{
    Json::Value arr(Json::arrayValue);
    if (joined.empty())
        return arr;
    size_t start = 0;
    while (true) {
        auto pos = joined.find('|', start);
        if (pos == std::string::npos) {
            arr.append(joined.substr(start));
            break;
        }
        arr.append(joined.substr(start, pos - start));
        start = pos + 1;
    }
    return arr;
}

const char *statusFor(bool hasContains, bool hasTrace)
{
    return hasContains ? "UNSAFE" : (hasTrace ? "CAUTION" : "SAFE");
}

int atoiOr(const std::string &s, int fallback)
{
    if (s.empty())
        return fallback;
    try {
        return std::stoi(s);
    } catch (...) {
        return fallback;
    }
}

Json::Value namesArray(const orm::Result &r)
{
    Json::Value a(Json::arrayValue);
    for (const auto &row : r)
        a.append(row["display_name"].as<std::string>());
    return a;
}

}  // namespace

void FoodController::search(const HttpRequestPtr &req,
                           std::function<void(const HttpResponsePtr &)> &&callback)
{
    int userId = req->attributes()->get<int>("user_id");
    std::string q = req->getParameter("q");
    std::string category = req->getParameter("category");
    std::string diet = req->getParameter("diet");
    // source = all | grocery | recipe. Groceries come from Open Food Facts,
    // recipes from Spoonacular; "all" merges both.
    std::string source = req->getParameter("source");
    if (source.empty())
        source = "all";
    const bool wantGrocery = (source == "all" || source == "grocery");
    const bool wantRecipe = (source == "all" || source == "recipe");
    // Pass as int (0/1), not bool: Drogon's binary bool binding misaligns params.
    int safeOnly = req->getParameter("safe_only") == "true" ? 1 : 0;
    int page = std::max(1, atoiOr(req->getParameter("page"), 1));
    int pageSize = std::min(std::max(atoiOr(req->getParameter("page_size"), 20), 1), 50);
    int offset = (page - 1) * pageSize;

    auto db = app().getDbClient();
    try {
        // Cache-first, per source: if nothing of that kind matches q locally, pull a
        // pool from the external API once and cache it. Later pages (Load more) are
        // served from that cached pool, so we don't re-hit the API per page.
        if (!q.empty()) {
            if (wantGrocery) {
                auto c = db->execSqlSync(
                    "SELECT count(*) AS n FROM food_items "
                    "WHERE kind = 'grocery' AND name ILIKE '%' || $1 || '%'", q);
                if (c[0]["n"].as<int64_t>() == 0) {
                    try {
                        auto prods = OffClient::instance().search(q, 25);
                        if (!prods.empty())
                            ingest::ingest(prods);
                    } catch (const std::exception &e) {
                        LOG_WARN << "OFF fetch failed: " << e.what();
                    }
                }
            }
            if (wantRecipe && SpoonacularClient::instance().enabled()) {
                auto c = db->execSqlSync(
                    "SELECT count(*) AS n FROM food_items "
                    "WHERE kind = 'recipe' AND name ILIKE '%' || $1 || '%'", q);
                if (c[0]["n"].as<int64_t>() == 0) {
                    try {
                        auto recipes = SpoonacularClient::instance().search(q, 25);
                        if (!recipes.empty())
                            ingest::ingestRecipes(recipes);
                    } catch (const std::exception &e) {
                        LOG_WARN << "Spoonacular fetch failed: " << e.what();
                    }
                }
            }
        }

        // Only the three user-supplied strings are bound as parameters ($1=q,
        // $2=category, $3=diet). The integers (userId, limit, offset, safe-only)
        // are server-controlled, so we inline them — this sidesteps a Drogon
        // binary-binding issue with mixed int params and keeps injection off the table.
        const std::string uid = std::to_string(userId);
        std::string sql =
            "SELECT f.id, f.name, f.brand, f.category, f.diet, f.image_url, f.kind, "
            "       string_agg(DISTINCT ca.display_name, '|') AS contains_names, "
            "       string_agg(DISTINCT ta.display_name, '|') AS trace_names "
            "FROM food_items f "
            "LEFT JOIN food_contains fc ON fc.food_id = f.id "
            "LEFT JOIN user_allergens uac ON uac.allergen_id = fc.allergen_id AND uac.user_id = " + uid + " "
            "LEFT JOIN allergens ca ON ca.id = uac.allergen_id "
            "LEFT JOIN food_traces ft ON ft.food_id = f.id "
            "LEFT JOIN user_allergens uat ON uat.allergen_id = ft.allergen_id AND uat.user_id = " + uid + " "
            "LEFT JOIN allergens ta ON ta.id = uat.allergen_id "
            "WHERE ($1 = '' OR f.name ILIKE '%' || $1 || '%') "
            "  AND ($2 = '' OR f.category = $2) "
            "  AND ($3 = '' OR f.diet = $3) ";
        // Kind filter (server-controlled literal; no user input inlined).
        if (wantGrocery && !wantRecipe)
            sql += "AND f.kind = 'grocery' ";
        else if (wantRecipe && !wantGrocery)
            sql += "AND f.kind = 'recipe' ";
        if (safeOnly) {
            sql +=
                "AND NOT EXISTS (SELECT 1 FROM food_contains x "
                "JOIN user_allergens u ON u.allergen_id = x.allergen_id AND u.user_id = " + uid + " "
                "WHERE x.food_id = f.id) "
                "AND NOT EXISTS (SELECT 1 FROM food_traces y "
                "JOIN user_allergens u ON u.allergen_id = y.allergen_id AND u.user_id = " + uid + " "
                "WHERE y.food_id = f.id) ";
        }
        // Fetch one extra row to learn whether a further page exists (Load more),
        // without a separate COUNT query.
        sql += "GROUP BY f.id ORDER BY f.name LIMIT " + std::to_string(pageSize + 1) +
               " OFFSET " + std::to_string(offset);

        auto rows = db->execSqlSync(sql, q, category, diet);
        const bool hasMore = static_cast<int>(rows.size()) > pageSize;

        Json::Value items(Json::arrayValue);
        int n = 0;
        for (const auto &row : rows) {
            if (n++ >= pageSize)
                break;  // the extra probe row is not returned
            std::string cont = fieldStr(row, "contains_names");
            std::string tr = fieldStr(row, "trace_names");
            Json::Value it;
            it["id"] = row["id"].as<int>();
            it["name"] = fieldStr(row, "name");
            it["brand"] = fieldStr(row, "brand");
            it["category"] = fieldStr(row, "category");
            it["diet"] = fieldStr(row, "diet");
            it["image_url"] = fieldStr(row, "image_url");
            it["kind"] = fieldStr(row, "kind");
            it["contains"] = pipeToArray(cont);
            it["may_contain"] = pipeToArray(tr);
            it["status"] = statusFor(!cont.empty(), !tr.empty());
            items.append(it);
        }

        Json::Value out;
        out["page"] = page;
        out["page_size"] = pageSize;
        out["count"] = static_cast<int>(items.size());
        out["has_more"] = hasMore;
        out["items"] = items;
        callback(HttpResponse::newHttpJsonResponse(out));
    } catch (const orm::DrogonDbException &e) {
        LOG_ERROR << "food search failed: " << e.base().what();
        callback(jsonError(k500InternalServerError, "database error"));
    }
}

void FoodController::filters(const HttpRequestPtr &req,
                            std::function<void(const HttpResponsePtr &)> &&callback)
{
    auto db = app().getDbClient();
    try {
        auto cats = db->execSqlSync(
            "SELECT DISTINCT category FROM food_items WHERE category IS NOT NULL ORDER BY category");
        auto diets = db->execSqlSync(
            "SELECT DISTINCT diet FROM food_items WHERE diet IS NOT NULL ORDER BY diet");

        Json::Value categories(Json::arrayValue), dietList(Json::arrayValue);
        for (const auto &r : cats)
            categories.append(r["category"].as<std::string>());
        for (const auto &r : diets)
            dietList.append(r["diet"].as<std::string>());

        Json::Value out;
        out["categories"] = categories;
        out["diets"] = dietList;
        callback(HttpResponse::newHttpJsonResponse(out));
    } catch (const orm::DrogonDbException &) {
        callback(jsonError(k500InternalServerError, "database error"));
    }
}

void FoodController::detail(const HttpRequestPtr &req,
                           std::function<void(const HttpResponsePtr &)> &&callback,
                           std::string id)
{
    int userId = req->attributes()->get<int>("user_id");
    int fid;
    try {
        fid = std::stoi(id);
    } catch (...) {
        callback(jsonError(k404NotFound, "not found"));
        return;
    }

    auto db = app().getDbClient();
    try {
        auto f = db->execSqlSync(
            "SELECT id, name, brand, category, diet, ingredients_text, image_url, kind "
            "FROM food_items WHERE id = $1",
            fid);
        if (f.size() == 0) {
            callback(jsonError(k404NotFound, "food not found"));
            return;
        }

        auto containsAll = db->execSqlSync(
            "SELECT a.display_name FROM food_contains fc JOIN allergens a ON a.id = fc.allergen_id "
            "WHERE fc.food_id = $1 ORDER BY a.display_name",
            fid);
        auto tracesAll = db->execSqlSync(
            "SELECT a.display_name FROM food_traces ft JOIN allergens a ON a.id = ft.allergen_id "
            "WHERE ft.food_id = $1 ORDER BY a.display_name",
            fid);
        auto yourContains = db->execSqlSync(
            "SELECT a.display_name FROM food_contains fc "
            "JOIN user_allergens ua ON ua.allergen_id = fc.allergen_id AND ua.user_id = $1 "
            "JOIN allergens a ON a.id = fc.allergen_id WHERE fc.food_id = $2 ORDER BY a.display_name",
            userId, fid);
        auto yourTraces = db->execSqlSync(
            "SELECT a.display_name FROM food_traces ft "
            "JOIN user_allergens ua ON ua.allergen_id = ft.allergen_id AND ua.user_id = $1 "
            "JOIN allergens a ON a.id = ft.allergen_id WHERE ft.food_id = $2 ORDER BY a.display_name",
            userId, fid);

        const auto &row = f[0];
        Json::Value out;
        out["id"] = row["id"].as<int>();
        out["name"] = fieldStr(row, "name");
        out["brand"] = fieldStr(row, "brand");
        out["category"] = fieldStr(row, "category");
        out["diet"] = fieldStr(row, "diet");
        out["ingredients_text"] = fieldStr(row, "ingredients_text");
        out["image_url"] = fieldStr(row, "image_url");
        out["kind"] = fieldStr(row, "kind");
        out["contains"] = namesArray(containsAll);
        out["traces"] = namesArray(tracesAll);

        Json::Value yours;
        yours["contains"] = namesArray(yourContains);
        yours["may_contain"] = namesArray(yourTraces);
        out["your_allergens"] = yours;
        out["status"] = statusFor(yourContains.size() > 0, yourTraces.size() > 0);

        callback(HttpResponse::newHttpJsonResponse(out));
    } catch (const orm::DrogonDbException &) {
        callback(jsonError(k500InternalServerError, "database error"));
    }
}

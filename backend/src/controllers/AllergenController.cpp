#include "controllers/AllergenController.h"

#include <drogon/drogon.h>
#include <drogon/orm/Exception.h>

#include "util/Responses.h"

using namespace drogon;
using namespace foodi;

// Public: the EU-14 list that populates the allergen picker.
void AllergenController::list(const HttpRequestPtr &req,
                             std::function<void(const HttpResponsePtr &)> &&callback)
{
    try {
        auto db = app().getDbClient();
        auto r = db->execSqlSync(
            "SELECT id, off_tag, display_name FROM allergens ORDER BY display_name");

        Json::Value arr(Json::arrayValue);
        for (const auto &row : r) {
            Json::Value a;
            a["id"] = row["id"].as<int>();
            a["off_tag"] = row["off_tag"].as<std::string>();
            a["display_name"] = row["display_name"].as<std::string>();
            arr.append(a);
        }
        callback(HttpResponse::newHttpJsonResponse(arr));
    } catch (const orm::DrogonDbException &) {
        callback(jsonError(k500InternalServerError, "database error"));
    }
}

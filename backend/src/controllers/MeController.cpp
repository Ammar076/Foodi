#include "controllers/MeController.h"

#include <drogon/drogon.h>
#include <drogon/orm/DbClient.h>
#include <drogon/orm/Exception.h>

#include "util/PasswordHasher.h"
#include "util/Responses.h"
#include "util/Strings.h"

using namespace drogon;
using namespace foodi;

namespace {

// Builds the full /me payload (profile + selected allergens). Reads via the
// supplied client/transaction so callers can read their own uncommitted writes.
Json::Value buildMe(const orm::DbClientPtr &db, int userId)
{
    auto u = db->execSqlSync(
        "SELECT id, email, username, profile_completed FROM users WHERE id = $1", userId);

    Json::Value out;
    out["id"] = u[0]["id"].as<int>();
    out["email"] = u[0]["email"].as<std::string>();
    out["username"] = u[0]["username"].as<std::string>();
    out["profile_completed"] = u[0]["profile_completed"].as<bool>();

    auto a = db->execSqlSync(
        "SELECT a.id, a.off_tag, a.display_name FROM user_allergens ua "
        "JOIN allergens a ON a.id = ua.allergen_id WHERE ua.user_id = $1 "
        "ORDER BY a.display_name",
        userId);

    Json::Value arr(Json::arrayValue);
    for (const auto &row : a) {
        Json::Value x;
        x["id"] = row["id"].as<int>();
        x["off_tag"] = row["off_tag"].as<std::string>();
        x["display_name"] = row["display_name"].as<std::string>();
        arr.append(x);
    }
    out["allergens"] = arr;
    return out;
}

}  // namespace

void MeController::getMe(const HttpRequestPtr &req,
                        std::function<void(const HttpResponsePtr &)> &&callback)
{
    int userId = req->attributes()->get<int>("user_id");
    try {
        callback(HttpResponse::newHttpJsonResponse(buildMe(app().getDbClient(), userId)));
    } catch (const orm::DrogonDbException &) {
        callback(jsonError(k500InternalServerError, "database error"));
    }
}

void MeController::updateMe(const HttpRequestPtr &req,
                           std::function<void(const HttpResponsePtr &)> &&callback)
{
    int userId = req->attributes()->get<int>("user_id");
    auto json = req->getJsonObject();
    if (!json) {
        callback(jsonError(k400BadRequest, "invalid JSON body"));
        return;
    }
    auto username = trim(json->get("username", "").asString());
    if (username.empty()) {
        callback(jsonError(k400BadRequest, "username is required"));
        return;
    }
    try {
        auto db = app().getDbClient();
        db->execSqlSync("UPDATE users SET username = $1 WHERE id = $2", username, userId);
        callback(HttpResponse::newHttpJsonResponse(buildMe(db, userId)));
    } catch (const orm::DrogonDbException &e) {
        std::string msg = e.base().what();
        if (msg.find("duplicate key") != std::string::npos)
            callback(jsonError(k409Conflict, "username already in use"));
        else
            callback(jsonError(k500InternalServerError, "database error"));
    }
}

void MeController::changePassword(const HttpRequestPtr &req,
                                 std::function<void(const HttpResponsePtr &)> &&callback)
{
    int userId = req->attributes()->get<int>("user_id");
    auto json = req->getJsonObject();
    if (!json) {
        callback(jsonError(k400BadRequest, "invalid JSON body"));
        return;
    }
    auto current = json->get("current_password", "").asString();
    auto next = json->get("new_password", "").asString();
    if (next.size() < 8) {
        callback(jsonError(k400BadRequest, "new_password must be at least 8 chars"));
        return;
    }
    try {
        auto db = app().getDbClient();
        auto r = db->execSqlSync("SELECT password_hash FROM users WHERE id = $1", userId);
        if (r.size() == 0 ||
            !password::verify(r[0]["password_hash"].as<std::string>(), current)) {
            callback(jsonError(k401Unauthorized, "current password is incorrect"));
            return;
        }
        auto hash = password::hash(next);
        if (hash.empty()) {
            callback(jsonError(k500InternalServerError, "could not hash password"));
            return;
        }
        db->execSqlSync("UPDATE users SET password_hash = $1 WHERE id = $2", hash, userId);
        Json::Value ok;
        ok["status"] = "password updated";
        callback(HttpResponse::newHttpJsonResponse(ok));
    } catch (const orm::DrogonDbException &) {
        callback(jsonError(k500InternalServerError, "database error"));
    }
}

void MeController::setAllergens(const HttpRequestPtr &req,
                               std::function<void(const HttpResponsePtr &)> &&callback)
{
    int userId = req->attributes()->get<int>("user_id");
    auto json = req->getJsonObject();
    if (!json || !(*json)["allergen_ids"].isArray()) {
        callback(jsonError(k400BadRequest, "allergen_ids (array of ids) is required"));
        return;
    }
    const auto &ids = (*json)["allergen_ids"];

    auto db = app().getDbClient();
    Json::Value me;
    {
        auto trans = db->newTransaction();
        try {
            trans->execSqlSync("DELETE FROM user_allergens WHERE user_id = $1", userId);
            for (const auto &v : ids) {
                trans->execSqlSync(
                    "INSERT INTO user_allergens (user_id, allergen_id) VALUES ($1, $2) "
                    "ON CONFLICT DO NOTHING",
                    userId, v.asInt());
            }
            // Saving allergens IS the "finish setup" step (an empty list is valid).
            trans->execSqlSync("UPDATE users SET profile_completed = true WHERE id = $1", userId);
            me = buildMe(trans, userId);  // read within the transaction
        } catch (const orm::DrogonDbException &) {
            trans->rollback();
            callback(jsonError(k400BadRequest, "invalid allergen id"));
            return;
        }
    }  // transaction commits here
    callback(HttpResponse::newHttpJsonResponse(me));
}

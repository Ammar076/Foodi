#include "controllers/AuthController.h"

#include <drogon/drogon.h>
#include <drogon/orm/Exception.h>
#include <sodium.h>

#include <cctype>
#include <cstdint>
#include <string>

#include "services/EmailClient.h"
#include "util/Jwt.h"
#include "util/PasswordHasher.h"
#include "util/Responses.h"
#include "util/Strings.h"

using namespace drogon;
using namespace foodi;

namespace {
Json::Value userJson(const orm::Row &row)
{
    Json::Value u;
    u["id"] = row["id"].as<int>();
    u["email"] = row["email"].as<std::string>();
    u["username"] = row["username"].as<std::string>();
    u["profile_completed"] = row["profile_completed"].as<bool>();
    return u;
}

// --- password-reset helpers (libsodium is already initialized in main()) ---

// SHA-256 (hex) of the input. We hash the high-entropy code rather than store it,
// so a DB leak yields no usable codes. A fast hash is correct here (unlike user
// passwords): the code is random, so it isn't vulnerable to dictionary attacks,
// and Argon2's slowness would only hurt the legitimate verify path.
std::string sha256Hex(const std::string &in)
{
    unsigned char h[crypto_hash_sha256_BYTES];
    crypto_hash_sha256(h, reinterpret_cast<const unsigned char *>(in.data()), in.size());
    char hex[crypto_hash_sha256_BYTES * 2 + 1];
    sodium_bin2hex(hex, sizeof(hex), h, sizeof(h));
    return std::string(hex);
}

// Uppercase + keep only A-Z/0-9, so "abcd-efgh" / "abcd efgh" all hash the same.
std::string normalizeCode(const std::string &raw)
{
    std::string out;
    for (unsigned char c : raw)
        if (std::isalnum(c))
            out += static_cast<char>(std::toupper(c));
    return out;
}

// 8 chars from a 31-symbol alphabet (no I/L/O/0/1 to avoid confusion) ≈ 2^39
// combinations — high enough that guessing is infeasible even before the attempt
// cap and expiry. randombytes_uniform is the unbiased CSPRNG from libsodium.
std::string generateResetCode()
{
    static const char alphabet[] = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";
    const uint32_t n = sizeof(alphabet) - 1;  // 31
    std::string code;
    code.reserve(8);
    for (int i = 0; i < 8; ++i)
        code += alphabet[randombytes_uniform(n)];
    return code;
}
}  // namespace

void AuthController::registerUser(const HttpRequestPtr &req,
                                 std::function<void(const HttpResponsePtr &)> &&callback)
{
    auto json = req->getJsonObject();
    if (!json) {
        callback(jsonError(k400BadRequest, "invalid JSON body"));
        return;
    }
    auto email = toLower(trim(json->get("email", "").asString()));
    auto username = trim(json->get("username", "").asString());
    auto password = json->get("password", "").asString();
    if (email.empty() || username.empty() || password.size() < 8) {
        callback(jsonError(k400BadRequest,
                           "email, username, and password (min 8 chars) are required"));
        return;
    }

    auto hash = password::hash(password);
    if (hash.empty()) {
        callback(jsonError(k500InternalServerError, "could not hash password"));
        return;
    }

    try {
        auto db = app().getDbClient();
        auto r = db->execSqlSync(
            "INSERT INTO users (email, username, password_hash) VALUES ($1, $2, $3) "
            "RETURNING id, email, username, profile_completed",
            email, username, hash);

        int id = r[0]["id"].as<int>();
        Json::Value out;
        out["token"] = jwtutil::issue(id);
        out["user"] = userJson(r[0]);
        auto resp = HttpResponse::newHttpJsonResponse(out);
        resp->setStatusCode(k201Created);
        callback(resp);
    } catch (const orm::DrogonDbException &e) {
        std::string msg = e.base().what();
        if (msg.find("duplicate key") != std::string::npos)
            callback(jsonError(k409Conflict, "email or username already in use"));
        else
            callback(jsonError(k500InternalServerError, "database error"));
    }
}

void AuthController::login(const HttpRequestPtr &req,
                          std::function<void(const HttpResponsePtr &)> &&callback)
{
    auto json = req->getJsonObject();
    if (!json) {
        callback(jsonError(k400BadRequest, "invalid JSON body"));
        return;
    }
    // Accept "login" (email or username), or explicit "email"/"username".
    std::string login = json->get("login", "").asString();
    if (login.empty())
        login = json->get("email", "").asString();
    if (login.empty())
        login = json->get("username", "").asString();
    login = trim(login);
    auto password = json->get("password", "").asString();
    if (login.empty() || password.empty()) {
        callback(jsonError(k400BadRequest, "login and password are required"));
        return;
    }

    try {
        auto db = app().getDbClient();
        auto r = db->execSqlSync(
            "SELECT id, email, username, password_hash, profile_completed FROM users "
            "WHERE email = $1 OR username = $2",
            toLower(login), login);

        if (r.size() == 0 ||
            !password::verify(r[0]["password_hash"].as<std::string>(), password)) {
            callback(jsonError(k401Unauthorized, "invalid credentials"));
            return;
        }

        int id = r[0]["id"].as<int>();
        Json::Value out;
        out["token"] = jwtutil::issue(id);
        out["user"] = userJson(r[0]);
        callback(HttpResponse::newHttpJsonResponse(out));
    } catch (const orm::DrogonDbException &) {
        callback(jsonError(k500InternalServerError, "database error"));
    }
}

void AuthController::forgotPassword(const HttpRequestPtr &req,
                                   std::function<void(const HttpResponsePtr &)> &&callback)
{
    auto json = req->getJsonObject();
    if (!json) {
        callback(jsonError(k400BadRequest, "invalid JSON body"));
        return;
    }
    std::string login = trim(json->get("login", "").asString());
    if (login.empty())
        login = trim(json->get("email", "").asString());
    if (login.empty()) {
        callback(jsonError(k400BadRequest, "email or username is required"));
        return;
    }

    // Return the SAME response whether or not the account exists, so this endpoint
    // can't be used to enumerate which emails/usernames are registered.
    Json::Value generic;
    generic["status"] = "If an account matches, a reset code has been sent.";
    auto genericResp = HttpResponse::newHttpJsonResponse(generic);

    try {
        auto db = app().getDbClient();
        auto r = db->execSqlSync(
            "SELECT id, email FROM users WHERE email = $1 OR username = $2",
            toLower(login), login);
        if (r.size() == 0) {
            callback(genericResp);  // unknown account: reveal nothing
            return;
        }
        int userId = r[0]["id"].as<int>();
        std::string email = r[0]["email"].as<std::string>();

        // Throttle: if a live code was issued in the last 60s, don't issue another
        // (stops this endpoint being used to flood someone's inbox).
        auto recent = db->execSqlSync(
            "SELECT 1 FROM password_reset_tokens WHERE user_id = $1 AND used_at IS NULL "
            "AND expires_at > now() AND created_at > now() - interval '60 seconds' LIMIT 1",
            userId);
        if (recent.size() > 0) {
            callback(genericResp);
            return;
        }

        const std::string code = generateResetCode();
        const std::string codeHash = sha256Hex(code);
        const int ttl = app().getCustomConfig().get("reset_code_ttl_minutes", 30).asInt();

        // Invalidate any prior outstanding codes, then store the new one's hash.
        db->execSqlSync("UPDATE password_reset_tokens SET used_at = now() "
                        "WHERE user_id = $1 AND used_at IS NULL",
                        userId);
        db->execSqlSync(
            "INSERT INTO password_reset_tokens (user_id, code_hash, expires_at) "
            "VALUES ($1, $2, now() + make_interval(mins => $3))",
            userId, codeHash, ttl);

        const std::string display = code.substr(0, 4) + "-" + code.substr(4);
        const std::string subject = "Your Foodi password reset code";
        const std::string body =
            "Hi,\r\n\r\nUse this code to reset your Foodi password:\r\n\r\n    " + display +
            "\r\n\r\nIt expires in " + std::to_string(ttl) +
            " minutes. If you didn't request this, you can ignore this email.\r\n";

        if (!EmailClient::instance().send(email, subject, body)) {
            // SMTP off or failed: log the code so the flow is still testable in dev.
            // Configure SMTP to actually email it; never return this to the client.
            LOG_WARN << "Password reset code for user " << userId << " (" << email
                     << "): " << display << "  [configure SMTP to email this]";
        }
        callback(genericResp);
    } catch (const orm::DrogonDbException &e) {
        LOG_ERROR << "forgotPassword DB error: " << e.base().what();
        callback(genericResp);  // stay generic even on internal failure
    }
}

void AuthController::resetPassword(const HttpRequestPtr &req,
                                  std::function<void(const HttpResponsePtr &)> &&callback)
{
    auto json = req->getJsonObject();
    if (!json) {
        callback(jsonError(k400BadRequest, "invalid JSON body"));
        return;
    }
    std::string login = trim(json->get("login", "").asString());
    if (login.empty())
        login = trim(json->get("email", "").asString());
    const std::string code = normalizeCode(json->get("code", "").asString());
    const std::string newPass = json->get("new_password", "").asString();
    if (login.empty() || code.empty()) {
        callback(jsonError(k400BadRequest, "login and code are required"));
        return;
    }
    if (newPass.size() < 8) {
        callback(jsonError(k400BadRequest, "new_password must be at least 8 chars"));
        return;
    }

    // One vague message for every "can't reset" case (no such account / wrong /
    // expired / used code) so we never reveal which part failed.
    const auto invalid = [&]() {
        callback(jsonError(k400BadRequest, "invalid or expired reset code"));
    };

    try {
        auto db = app().getDbClient();
        auto u = db->execSqlSync("SELECT id FROM users WHERE email = $1 OR username = $2",
                                 toLower(login), login);
        if (u.size() == 0) {
            invalid();
            return;
        }
        int userId = u[0]["id"].as<int>();

        auto t = db->execSqlSync(
            "SELECT id, code_hash, attempts FROM password_reset_tokens "
            "WHERE user_id = $1 AND used_at IS NULL AND expires_at > now() "
            "ORDER BY created_at DESC LIMIT 1",
            userId);
        if (t.size() == 0) {
            invalid();
            return;
        }

        const int tokenId = t[0]["id"].as<int>();
        const int attempts = t[0]["attempts"].as<int>();
        constexpr int kMaxAttempts = 5;
        if (attempts >= kMaxAttempts) {
            db->execSqlSync("UPDATE password_reset_tokens SET used_at = now() WHERE id = $1",
                            tokenId);
            callback(jsonError(k400BadRequest, "too many attempts; request a new code"));
            return;
        }

        const std::string stored = t[0]["code_hash"].as<std::string>();
        const std::string given = sha256Hex(code);
        const bool match = stored.size() == given.size() &&
                           sodium_memcmp(stored.data(), given.data(), given.size()) == 0;
        if (!match) {
            db->execSqlSync(
                "UPDATE password_reset_tokens SET attempts = attempts + 1 WHERE id = $1",
                tokenId);
            invalid();
            return;
        }

        const auto hash = password::hash(newPass);
        if (hash.empty()) {
            callback(jsonError(k500InternalServerError, "could not hash password"));
            return;
        }

        // Update the password and burn this + any other live codes, atomically.
        auto trans = db->newTransaction();
        trans->execSqlSync("UPDATE users SET password_hash = $1 WHERE id = $2", hash, userId);
        trans->execSqlSync("UPDATE password_reset_tokens SET used_at = now() "
                           "WHERE user_id = $1 AND used_at IS NULL",
                           userId);

        Json::Value ok;
        ok["status"] = "password updated";
        callback(HttpResponse::newHttpJsonResponse(ok));
    } catch (const orm::DrogonDbException &e) {
        LOG_ERROR << "resetPassword DB error: " << e.base().what();
        callback(jsonError(k500InternalServerError, "database error"));
    }
}

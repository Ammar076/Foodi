#include <drogon/drogon.h>

#include <cstdlib>
#include <filesystem>
#include <string>

#include "util/Jwt.h"
#include "util/PasswordHasher.h"

// Returns the environment variable if set and non-empty, otherwise the fallback.
// Secrets come from the environment in production; config.json supplies a fallback
// for local dev convenience.
static std::string envOr(const char *key, const std::string &fallback)
{
    const char *v = std::getenv(key);
    return (v && *v) ? std::string(v) : fallback;
}

int main()
{
    namespace fs = std::filesystem;
    if (!fs::exists("config.json")) {
        LOG_ERROR << "config.json not found (copy config.example.json -> config.json)";
        return 1;
    }
    drogon::app().loadConfigFile("config.json");

    if (!foodi::password::init()) {
        LOG_FATAL << "libsodium initialization failed";
        return 1;
    }

    const auto &cfg = drogon::app().getCustomConfig();
    std::string dbHost = cfg.get("db_host", "localhost").asString();
    auto dbPort = static_cast<unsigned short>(cfg.get("db_port", 5432).asUInt());
    std::string dbName = cfg.get("db_name", "foodi").asString();
    std::string dbUser = cfg.get("db_user", "foodi").asString();
    std::string dbPassword = envOr("FOODI_DB_PASSWORD", cfg.get("db_password", "").asString());
    std::string jwtSecret = envOr("FOODI_JWT_SECRET", cfg.get("jwt_secret", "").asString());
    int jwtExpiry = cfg.get("jwt_expiry_hours", 1).asInt();

    if (jwtSecret.empty()) {
        LOG_FATAL << "JWT secret missing (set FOODI_JWT_SECRET or config jwt_secret)";
        return 1;
    }
    if (dbPassword.empty()) {
        LOG_FATAL << "DB password missing (set FOODI_DB_PASSWORD or config db_password)";
        return 1;
    }

    foodi::jwtutil::init(jwtSecret, jwtExpiry);
    drogon::app().createDbClient("postgresql", dbHost, dbPort, dbName, dbUser, dbPassword, 2);

    drogon::app().registerHandler(
        "/health",
        [](const drogon::HttpRequestPtr &,
           std::function<void(const drogon::HttpResponsePtr &)> &&callback) {
            Json::Value json;
            json["status"] = "ok";
            json["service"] = "foodi-backend";
            json["phase"] = 1;
            callback(drogon::HttpResponse::newHttpJsonResponse(json));
        },
        {drogon::Get});

    LOG_INFO << "Foodi backend (phase 1) listening on http://127.0.0.1:8080";
    drogon::app().run();
    return 0;
}

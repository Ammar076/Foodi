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
    // config.json holds non-secret service settings (SMTP host, OFF URL, …). It is
    // optional: in a hosted container everything that matters can come from env.
    if (fs::exists("config.json"))
        drogon::app().loadConfigFile("config.json");
    else
        LOG_WARN << "config.json not found; relying on environment variables";

    if (!foodi::password::init()) {
        LOG_FATAL << "libsodium initialization failed";
        return 1;
    }

    const auto &cfg = drogon::app().getCustomConfig();
    // DB connection: env first (FOODI_DB_*), config.json as the local-dev fallback.
    // For a managed Postgres that requires TLS (e.g. Neon), also set PGSSLMODE=require
    // in the environment — libpq reads it (Drogon doesn't expose sslmode directly).
    std::string dbHost = envOr("FOODI_DB_HOST", cfg.get("db_host", "localhost").asString());
    std::string dbPortStr = envOr("FOODI_DB_PORT", std::to_string(cfg.get("db_port", 5432).asUInt()));
    auto dbPort = static_cast<unsigned short>(std::stoi(dbPortStr));
    std::string dbName = envOr("FOODI_DB_NAME", cfg.get("db_name", "foodi").asString());
    std::string dbUser = envOr("FOODI_DB_USER", cfg.get("db_user", "foodi").asString());
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

    // Listen port: PaaS platforms inject PORT (e.g. Render); FOODI_PORT overrides;
    // default 8080 for local runs. Bound programmatically so config.json carries no
    // listener (avoids a double bind).
    int port = 8080;
    try {
        std::string p = envOr("FOODI_PORT", envOr("PORT", "8080"));
        port = std::stoi(p);
    } catch (...) {
        port = 8080;
    }

    foodi::jwtutil::init(jwtSecret, jwtExpiry);
    drogon::app().createDbClient("postgresql", dbHost, dbPort, dbName, dbUser, dbPassword, 2);
    drogon::app().addListener("0.0.0.0", port);

    drogon::app().registerHandler(
        "/health",
        [](const drogon::HttpRequestPtr &,
           std::function<void(const drogon::HttpResponsePtr &)> &&callback) {
            Json::Value json;
            json["status"] = "ok";
            json["service"] = "foodi-backend";
            callback(drogon::HttpResponse::newHttpJsonResponse(json));
        },
        {drogon::Get});

    LOG_INFO << "Foodi backend listening on 0.0.0.0:" << port;
    drogon::app().run();
    return 0;
}

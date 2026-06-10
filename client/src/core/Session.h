#pragma once
#include <QJsonObject>
#include <QSettings>
#include <QString>

// Backend URL is baked in at build time (CMake FOODI_API_URL); defaults to
// localhost for dev builds. main() additionally honours a FOODI_API_URL env var.
#ifndef FOODI_DEFAULT_API_URL
#define FOODI_DEFAULT_API_URL "http://127.0.0.1:8080"
#endif

// Holds auth state + the current user for the lifetime of the app. Shared by
// pointer between the API client and the windows so everyone reads the same
// token/user. The token lives in memory; "Remember me" additionally persists it
// via QSettings so a returning user is auto-validated on launch (see main()).
struct Session {
    QString baseUrl = QStringLiteral(FOODI_DEFAULT_API_URL);
    QString token;
    int userId = 0;
    QString username;
    QString email;
    bool profileCompleted = false;

    bool isAuthenticated() const { return !token.isEmpty(); }

    // Parse the {"token":..., "user":{...}} payload returned by register/login.
    void setFromAuthResponse(const QJsonObject &root)
    {
        token = root.value("token").toString();
        setFromMe(root.value("user").toObject());
    }

    // Populate the user fields from a GET /api/me object (token must already be set).
    // Used by the "Remember me" auto-login path on startup.
    void setFromMe(const QJsonObject &u)
    {
        userId = u.value("id").toInt();
        username = u.value("username").toString();
        email = u.value("email").toString();
        profileCompleted = u.value("profile_completed").toBool();
    }

    // "Remember me" persistence via QSettings (needs org/app name set in main()).
    // NOTE: QSettings stores in the registry in plaintext — fine for a local
    // desktop app, but a hardened build would use the OS credential store / DPAPI.
    void persist() const
    {
        QSettings s;
        s.setValue("auth/token", token);
    }
    void forgetPersisted() const
    {
        QSettings s;
        s.remove("auth/token");
    }
    static QString persistedToken()
    {
        QSettings s;
        return s.value("auth/token").toString();
    }

    void clear()
    {
        token.clear();
        userId = 0;
        username.clear();
        email.clear();
        profileCompleted = false;
    }
};

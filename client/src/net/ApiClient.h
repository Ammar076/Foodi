#pragma once
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>

#include <functional>

#include "core/Session.h"

class QNetworkReply;

// Thin async wrapper over QNetworkAccessManager. Every call takes a callback that
// fires when the response arrives: (ok, body, error). `ok` is true on HTTP 2xx;
// on failure, `error` holds a user-facing message (the backend's {"error":...}
// text, or a transport message if the server is unreachable).
//
// We use Qt's async networking here rather than libcurl (as the backend does):
// the client is already an event-loop app, so non-blocking requests that keep the
// UI responsive are exactly what we want. Tool fits the context.
class ApiClient : public QObject
{
    Q_OBJECT
public:
    using Callback =
        std::function<void(bool ok, const QJsonDocument &body, const QString &error)>;

    explicit ApiClient(Session *session, QObject *parent = nullptr);

    void registerUser(const QString &username, const QString &email,
                      const QString &password, Callback cb);
    void login(const QString &loginId, const QString &password, Callback cb);

    // Password reset: request a one-time code (emailed), then redeem it.
    void forgotPassword(const QString &login, Callback cb);
    void resetPassword(const QString &login, const QString &code,
                       const QString &newPassword, Callback cb);

    // Generic verbs reused by later slices (allergens, /api/me, /api/foods, ...).
    void getJson(const QString &path, Callback cb);
    void putJson(const QString &path, const QJsonObject &body, Callback cb);
    void postJson(const QString &path, const QJsonObject &body, Callback cb);

private:
    void send(const QByteArray &verb, const QString &path, const QJsonObject &body,
              Callback cb);
    void handle(QNetworkReply *reply, const Callback &cb);

    QNetworkAccessManager nam_;
    Session *session_;
};

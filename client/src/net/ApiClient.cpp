#include "net/ApiClient.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

ApiClient::ApiClient(Session *session, QObject *parent)
    : QObject(parent), session_(session)
{
}

void ApiClient::registerUser(const QString &username, const QString &email,
                             const QString &password, Callback cb)
{
    const QJsonObject body{
        {"username", username},
        {"email", email},
        {"password", password},
    };
    postJson("/api/auth/register", body, std::move(cb));
}

void ApiClient::login(const QString &loginId, const QString &password, Callback cb)
{
    const QJsonObject body{
        {"login", loginId},
        {"password", password},
    };
    postJson("/api/auth/login", body, std::move(cb));
}

void ApiClient::forgotPassword(const QString &login, Callback cb)
{
    postJson("/api/auth/forgot-password", QJsonObject{{"login", login}}, std::move(cb));
}

void ApiClient::resetPassword(const QString &login, const QString &code,
                              const QString &newPassword, Callback cb)
{
    const QJsonObject body{
        {"login", login},
        {"code", code},
        {"new_password", newPassword},
    };
    postJson("/api/auth/reset-password", body, std::move(cb));
}

void ApiClient::getJson(const QString &path, Callback cb)
{
    send("GET", path, {}, std::move(cb));
}

void ApiClient::putJson(const QString &path, const QJsonObject &body, Callback cb)
{
    send("PUT", path, body, std::move(cb));
}

void ApiClient::postJson(const QString &path, const QJsonObject &body, Callback cb)
{
    send("POST", path, body, std::move(cb));
}

void ApiClient::send(const QByteArray &verb, const QString &path,
                     const QJsonObject &body, Callback cb)
{
    QNetworkRequest req{QUrl(session_->baseUrl + path)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    // Attach the bearer token whenever we have one. Auth endpoints ignore it;
    // everything else requires it.
    if (!session_->token.isEmpty())
        req.setRawHeader("Authorization", "Bearer " + session_->token.toUtf8());

    const QByteArray data =
        body.isEmpty() ? QByteArray() : QJsonDocument(body).toJson(QJsonDocument::Compact);

    QNetworkReply *reply = nullptr;
    if (verb == "GET")
        reply = nam_.get(req);
    else if (verb == "POST")
        reply = nam_.post(req, data);
    else if (verb == "PUT")
        reply = nam_.put(req, data);
    else
        reply = nam_.sendCustomRequest(req, verb, data);

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, cb = std::move(cb)]() { handle(reply, cb); });
}

void ApiClient::handle(QNetworkReply *reply, const Callback &cb)
{
    reply->deleteLater();
    const QByteArray raw = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &perr);

    // No HTTP status at all -> transport failure (server down, refused, DNS).
    if (status == 0) {
        cb(false, doc, QStringLiteral("Cannot reach server: %1").arg(reply->errorString()));
        return;
    }
    if (status >= 200 && status < 300) {
        cb(true, doc, QString());
        return;
    }
    // Error: backend sends {"error":"..."} — surface that message.
    QString msg = doc.object().value("error").toString();
    if (msg.isEmpty())
        msg = QStringLiteral("Request failed (HTTP %1)").arg(status);
    cb(false, doc, msg);
}

#include "services/EmailClient.h"

#include <curl/curl.h>
#include <drogon/drogon.h>  // app().getCustomConfig(), LOG_* macros

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

using namespace drogon;

namespace {

// Secrets come from the environment in production; config.json is the dev fallback.
std::string envOr(const char *key, const std::string &fallback)
{
    const char *v = std::getenv(key);
    return (v && *v) ? std::string(v) : fallback;
}

// libcurl pulls the message body from us through this read callback as it uploads
// to the SMTP server. We hand it the raw RFC 5322 message and track our position.
struct Payload {
    std::string data;
    size_t pos = 0;
};

size_t readPayload(char *buffer, size_t size, size_t nitems, void *userdata)
{
    auto *p = static_cast<Payload *>(userdata);
    size_t room = size * nitems;
    size_t left = p->data.size() - p->pos;
    size_t n = left < room ? left : room;
    if (n > 0) {
        std::memcpy(buffer, p->data.data() + p->pos, n);
        p->pos += n;
    }
    return n;  // 0 signals end-of-message
}

// RFC 5322 Date header in UTC, e.g. "Tue, 15 Nov 1994 08:12:31 +0000".
std::string rfc5322Date()
{
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S +0000", &tm);
    return buf;
}

}  // namespace

namespace foodi {

EmailClient &EmailClient::instance()
{
    static EmailClient inst;
    return inst;
}

EmailClient::EmailClient()
{
    // Refcounted; OffClient may also call this. Matched cleanup is left to process
    // exit (same as OffClient).
    curl_global_init(CURL_GLOBAL_DEFAULT);

    const auto &cfg = app().getCustomConfig();
    enabled_ = cfg.get("smtp_enabled", false).asBool();
    host_ = cfg.get("smtp_host", "").asString();
    port_ = cfg.get("smtp_port", 587).asInt();
    security_ = cfg.get("smtp_security", "starttls").asString();
    user_ = cfg.get("smtp_user", "").asString();
    from_ = cfg.get("smtp_from", user_).asString();
    fromName_ = cfg.get("smtp_from_name", "Foodi").asString();
    password_ = envOr("FOODI_SMTP_PASSWORD", cfg.get("smtp_password", "").asString());
}

bool EmailClient::send(const std::string &toEmail, const std::string &subject,
                       const std::string &body)
{
    if (!enabled_) {
        LOG_WARN << "EmailClient: smtp_enabled=false; not sending email";
        return false;
    }
    if (host_.empty() || from_.empty()) {
        LOG_ERROR << "EmailClient: smtp_host / smtp_from not configured";
        return false;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        LOG_ERROR << "EmailClient: curl_easy_init failed";
        return false;
    }

    const bool implicitTls = (security_ == "ssl" || security_ == "smtps");
    const std::string url =
        (implicitTls ? "smtps://" : "smtp://") + host_ + ":" + std::to_string(port_);

    // Build the message: headers, a blank line, then the body — CRLF throughout,
    // as the SMTP/RFC 5322 wire format requires.
    Payload payload;
    payload.data = "Date: " + rfc5322Date() + "\r\n" +
                   "From: " + fromName_ + " <" + from_ + ">\r\n" +
                   "To: <" + toEmail + ">\r\n" +
                   "Subject: " + subject + "\r\n" +
                   "MIME-Version: 1.0\r\n" +
                   "Content-Type: text/plain; charset=UTF-8\r\n" +
                   "\r\n" + body + "\r\n";

    struct curl_slist *recipients = nullptr;
    recipients = curl_slist_append(recipients, ("<" + toEmail + ">").c_str());
    const std::string mailFrom = "<" + from_ + ">";

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    // CURLUSESSL_ALL: for smtp:// this requires a STARTTLS upgrade; for smtps://
    // the socket is already TLS. Either way we refuse to send in cleartext.
    curl_easy_setopt(curl, CURLOPT_USE_SSL, static_cast<long>(CURLUSESSL_ALL));
    if (!user_.empty()) {
        curl_easy_setopt(curl, CURLOPT_USERNAME, user_.c_str());
        curl_easy_setopt(curl, CURLOPT_PASSWORD, password_.c_str());
    }
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, mailFrom.c_str());
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, readPayload);
    curl_easy_setopt(curl, CURLOPT_READDATA, &payload);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

    CURLcode rc = curl_easy_perform(curl);
    curl_slist_free_all(recipients);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
        LOG_ERROR << "EmailClient: SMTP send to " << toEmail
                  << " failed: " << curl_easy_strerror(rc);
        return false;
    }
    LOG_INFO << "EmailClient: reset email sent to " << toEmail;
    return true;
}

}  // namespace foodi

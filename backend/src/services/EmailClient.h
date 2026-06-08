#pragma once
#include <string>

namespace foodi {

// Sends transactional email (currently just password-reset codes) over SMTP using
// libcurl. We reuse libcurl — already a dependency for Open Food Facts — because
// it speaks SMTP/SMTPS and uses the OS TLS stack (schannel on Windows), so there
// is no extra library and no CA-bundle wrangling. The send is synchronous and is
// only hit on the (rare) password-reset path, which runs on a Drogon worker
// thread, so blocking briefly there is acceptable.
//
// Configuration (custom_config in config.json / env):
//   smtp_enabled   - master switch. When false, send() is a no-op returning false
//                    so the caller can fall back to logging the code in dev.
//   smtp_host      - e.g. "smtp.gmail.com"
//   smtp_port      - 587 (STARTTLS) or 465 (implicit TLS)
//   smtp_security  - "starttls" (default) or "ssl"
//   smtp_user      - SMTP login (often the full from-address)
//   smtp_from      - envelope/From address (defaults to smtp_user)
//   smtp_from_name - display name in the From header (default "Foodi")
//   smtp_password  - SECRET: read from FOODI_SMTP_PASSWORD env, else config.
class EmailClient
{
public:
    static EmailClient &instance();

    bool enabled() const { return enabled_; }

    // Returns true if the SMTP server accepted the message. Logs and returns false
    // on any failure; callers must NOT surface that detail to clients (it would
    // leak whether an account exists / internal config state).
    bool send(const std::string &toEmail, const std::string &subject,
              const std::string &body);

private:
    EmailClient();
    EmailClient(const EmailClient &) = delete;
    EmailClient &operator=(const EmailClient &) = delete;

    bool enabled_ = false;
    std::string host_;
    long port_ = 587;
    std::string security_;  // "starttls" or "ssl"
    std::string user_;
    std::string password_;
    std::string from_;
    std::string fromName_;
};

}  // namespace foodi

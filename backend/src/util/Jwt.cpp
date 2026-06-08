#include "util/Jwt.h"

#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/open-source-parsers-jsoncpp/traits.h>

#include <chrono>

// This vcpkg build of jwt-cpp doesn't ship the default picojson traits, so we
// bind explicitly to the JsonCpp traits (JsonCpp is already pulled in by Drogon).
namespace {
std::string g_secret;
int g_expiryHours = 1;
const std::string kIssuer = "foodi";
using traits = jwt::traits::open_source_parsers_jsoncpp;
}  // namespace

namespace foodi::jwtutil {

void init(const std::string &secret, int expiryHours)
{
    g_secret = secret;
    g_expiryHours = expiryHours > 0 ? expiryHours : 1;
}

std::string issue(int userId)
{
    auto now = std::chrono::system_clock::now();
    return jwt::create<traits>()
        .set_issuer(kIssuer)
        .set_type("JWS")
        .set_subject(std::to_string(userId))
        .set_issued_at(now)
        .set_expires_at(now + std::chrono::hours(g_expiryHours))
        .sign(jwt::algorithm::hs256{g_secret});
}

std::optional<int> verify(const std::string &token)
{
    try {
        auto decoded = jwt::decode<traits>(token);
        jwt::verify<traits>()
            .allow_algorithm(jwt::algorithm::hs256{g_secret})
            .with_issuer(kIssuer)
            .verify(decoded);
        return std::stoi(decoded.get_subject());
    } catch (...) {
        return std::nullopt;
    }
}

}  // namespace foodi::jwtutil

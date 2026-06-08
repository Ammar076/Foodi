#include "filters/JwtAuthFilter.h"

#include "util/Jwt.h"
#include "util/Responses.h"

using namespace drogon;

void JwtAuthFilter::doFilter(const HttpRequestPtr &req,
                             FilterCallback &&fcb,
                             FilterChainCallback &&fccb)
{
    auto auth = req->getHeader("Authorization");
    const std::string prefix = "Bearer ";
    if (auth.size() > prefix.size() && auth.compare(0, prefix.size(), prefix) == 0) {
        auto userId = foodi::jwtutil::verify(auth.substr(prefix.size()));
        if (userId) {
            req->attributes()->insert("user_id", *userId);
            fccb();  // continue to the controller
            return;
        }
    }
    fcb(foodi::jsonError(k401Unauthorized, "unauthorized"));
}

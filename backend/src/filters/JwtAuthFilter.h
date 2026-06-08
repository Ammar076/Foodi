#pragma once
#include <drogon/HttpFilter.h>

// Guards protected routes: validates the Bearer JWT and injects "user_id"
// into the request attributes for downstream controllers.
class JwtAuthFilter : public drogon::HttpFilter<JwtAuthFilter>
{
public:
    void doFilter(const drogon::HttpRequestPtr &req,
                  drogon::FilterCallback &&fcb,
                  drogon::FilterChainCallback &&fccb) override;
};

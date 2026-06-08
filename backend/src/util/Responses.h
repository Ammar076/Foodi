#pragma once
#include <drogon/HttpResponse.h>
#include <json/json.h>
#include <string>

namespace foodi {

// Small helper for consistent JSON error bodies: {"error": "..."}.
inline drogon::HttpResponsePtr jsonError(drogon::HttpStatusCode code, const std::string &message)
{
    Json::Value j;
    j["error"] = message;
    auto resp = drogon::HttpResponse::newHttpJsonResponse(j);
    resp->setStatusCode(code);
    return resp;
}

}  // namespace foodi

#pragma once
#include <drogon/HttpController.h>

// All routes require a valid JWT (JwtAuthFilter injects "user_id").
class MeController : public drogon::HttpController<MeController>
{
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(MeController::getMe, "/api/me", drogon::Get, "JwtAuthFilter");
    ADD_METHOD_TO(MeController::updateMe, "/api/me", drogon::Put, "JwtAuthFilter");
    ADD_METHOD_TO(MeController::changePassword, "/api/me/password", drogon::Put, "JwtAuthFilter");
    ADD_METHOD_TO(MeController::setAllergens, "/api/me/allergens", drogon::Put, "JwtAuthFilter");
    METHOD_LIST_END

    void getMe(const drogon::HttpRequestPtr &req,
               std::function<void(const drogon::HttpResponsePtr &)> &&callback);
    void updateMe(const drogon::HttpRequestPtr &req,
                  std::function<void(const drogon::HttpResponsePtr &)> &&callback);
    void changePassword(const drogon::HttpRequestPtr &req,
                        std::function<void(const drogon::HttpResponsePtr &)> &&callback);
    void setAllergens(const drogon::HttpRequestPtr &req,
                      std::function<void(const drogon::HttpResponsePtr &)> &&callback);
};

#pragma once
#include <drogon/HttpController.h>

// All routes require a valid JWT (status is computed for the logged-in user).
class FoodController : public drogon::HttpController<FoodController>
{
public:
    METHOD_LIST_BEGIN
    // Static routes before the parametrized one so "filters" isn't read as an id.
    ADD_METHOD_TO(FoodController::filters, "/api/foods/filters", drogon::Get, "JwtAuthFilter");
    ADD_METHOD_TO(FoodController::search, "/api/foods", drogon::Get, "JwtAuthFilter");
    ADD_METHOD_TO(FoodController::detail, "/api/foods/{1}", drogon::Get, "JwtAuthFilter");
    METHOD_LIST_END

    void search(const drogon::HttpRequestPtr &req,
                std::function<void(const drogon::HttpResponsePtr &)> &&callback);
    void filters(const drogon::HttpRequestPtr &req,
                 std::function<void(const drogon::HttpResponsePtr &)> &&callback);
    void detail(const drogon::HttpRequestPtr &req,
                std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                std::string id);
};

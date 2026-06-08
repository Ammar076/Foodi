#pragma once
#include <drogon/HttpController.h>

class AllergenController : public drogon::HttpController<AllergenController>
{
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(AllergenController::list, "/api/allergens", drogon::Get);
    METHOD_LIST_END

    void list(const drogon::HttpRequestPtr &req,
              std::function<void(const drogon::HttpResponsePtr &)> &&callback);
};

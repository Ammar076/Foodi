#pragma once
#include <optional>
#include <string>

// Thin wrapper around jwt-cpp for issuing/verifying HS256 tokens.
// init() must be called once at startup with the signing secret.
namespace foodi::jwtutil {

void init(const std::string &secret, int expiryHours);

// Issues a signed token whose subject is the user id.
std::string issue(int userId);

// Verifies signature, issuer, and expiry; returns the user id on success.
std::optional<int> verify(const std::string &token);

}  // namespace foodi::jwtutil

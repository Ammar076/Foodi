#pragma once
#include <string>

// Argon2id password hashing via libsodium's crypto_pwhash.
namespace foodi::password {

bool init();  // wraps sodium_init(); call once at startup

// Returns the encoded Argon2id string (salt + params embedded), or "" on failure.
std::string hash(const std::string &plain);

// Verifies a plaintext password against a stored encoded hash.
bool verify(const std::string &encoded, const std::string &plain);

}  // namespace foodi::password

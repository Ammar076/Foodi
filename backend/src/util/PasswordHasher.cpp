#include "util/PasswordHasher.h"

#include <sodium.h>

namespace foodi::password {

bool init()
{
    return sodium_init() >= 0;
}

std::string hash(const std::string &plain)
{
    std::string out;
    out.resize(crypto_pwhash_STRBYTES);
    if (crypto_pwhash_str(out.data(),
                          plain.c_str(), plain.size(),
                          crypto_pwhash_OPSLIMIT_INTERACTIVE,
                          crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0) {
        return {};  // out of memory
    }
    out.resize(std::char_traits<char>::length(out.c_str()));  // trim padding to the real string
    return out;
}

bool verify(const std::string &encoded, const std::string &plain)
{
    return crypto_pwhash_str_verify(encoded.c_str(), plain.c_str(), plain.size()) == 0;
}

}  // namespace foodi::password

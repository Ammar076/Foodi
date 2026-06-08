#pragma once
#include <algorithm>
#include <cctype>
#include <string>

namespace foodi {

inline std::string toLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

inline std::string trim(const std::string &s)
{
    const char *ws = " \t\r\n";
    size_t b = s.find_first_not_of(ws);
    if (b == std::string::npos)
        return "";
    size_t e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

}  // namespace foodi

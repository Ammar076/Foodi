#pragma once
#include <string>
#include <vector>

namespace foodi {

// A product fetched from Open Food Facts, normalized to what we cache.
struct OffProduct {
    std::string barcode;
    std::string name;
    std::string brand;
    std::string category;          // first OFF category, made readable
    std::string diet;              // "vegan" / "vegetarian" / "" (from ingredients_analysis)
    std::string ingredients_text;
    std::string image_url;
    std::vector<std::string> allergen_tags;  // OFF tags, e.g. "en:milk"
    std::vector<std::string> trace_tags;     // OFF traces tags
};

// Talks to Open Food Facts over libcurl. We call OFF synchronously from request
// handlers (cache-miss path), and libcurl's easy interface is a natural fit:
// it uses the OS network/TLS/DNS stack — the same one `curl.exe` uses — which
// avoids trantor's async resolver and is portable to the Linux deploy target.
class OffClient
{
public:
    static OffClient &instance();
    std::vector<OffProduct> search(const std::string &query, int limit);

private:
    OffClient();
    OffClient(const OffClient &) = delete;
    OffClient &operator=(const OffClient &) = delete;

    std::string baseUrl_;
    std::string userAgent_;
};

}  // namespace foodi

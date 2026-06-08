#pragma once
#include <vector>

#include "services/OffClient.h"

namespace foodi::ingest {

// Upserts OFF products into the cache (food_items + food_contains/food_traces),
// resolving allergens from OFF tags with a synonym-dictionary fallback over the
// ingredient text. Returns the number of products successfully written.
int ingest(const std::vector<OffProduct> &products);

}  // namespace foodi::ingest

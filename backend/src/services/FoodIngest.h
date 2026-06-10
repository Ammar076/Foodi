#pragma once
#include <vector>

#include "services/OffClient.h"
#include "services/SpoonacularClient.h"

namespace foodi::ingest {

// Upserts OFF products into the cache (food_items + food_contains/food_traces),
// resolving allergens from OFF tags with a synonym-dictionary fallback over the
// ingredient text. Returns the number of products successfully written.
int ingest(const std::vector<OffProduct> &products);

// Upserts Spoonacular recipes (kind='recipe', deduped on spoonacular_id). Recipes
// carry no structured allergen tags, so allergens are resolved best-effort by
// scanning the joined ingredient text with the synonym dictionary. Returns the
// number of recipes successfully written.
int ingestRecipes(const std::vector<SpoonacularRecipe> &recipes);

}  // namespace foodi::ingest

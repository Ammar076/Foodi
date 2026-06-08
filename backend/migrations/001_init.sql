-- Foodi schema — Phase 1
-- Apply:  psql -h localhost -p 5433 -U foodi -d foodi -v ON_ERROR_STOP=1 -f migrations/001_init.sql
-- Idempotent: safe to re-run (CREATE ... IF NOT EXISTS).

BEGIN;

-- Canonical allergen list (seeded with the EU 14 in 002). off_tag matches the
-- Open Food Facts taxonomy (e.g. 'en:milk') so user picks, food data, and the
-- matching vocabulary are all the same set of ids.
CREATE TABLE IF NOT EXISTS allergens (
    id           SERIAL PRIMARY KEY,
    off_tag      TEXT UNIQUE NOT NULL,
    display_name TEXT NOT NULL
);

-- Synonym / derived-ingredient dictionary, used as a fallback to scan raw
-- ingredient text when a product's structured OFF allergen tags are sparse.
-- Terms are stored normalized (lowercase). The matcher compares whole
-- normalized tokens against these terms (NOT arbitrary substrings), so 'egg'
-- here will not falsely match 'eggplant'.
CREATE TABLE IF NOT EXISTS allergen_synonyms (
    id          SERIAL PRIMARY KEY,
    allergen_id INTEGER NOT NULL REFERENCES allergens(id) ON DELETE CASCADE,
    term        TEXT NOT NULL,
    UNIQUE (allergen_id, term)
);
CREATE INDEX IF NOT EXISTS idx_allergen_synonyms_term ON allergen_synonyms (term);

-- Accounts. password_hash holds the full Argon2id encoded string (salt + params
-- embedded). email/username are normalized (lowercased/trimmed) by the backend
-- before insert so the UNIQUE constraints are effectively case-insensitive.
CREATE TABLE IF NOT EXISTS users (
    id            SERIAL PRIMARY KEY,
    email         TEXT UNIQUE NOT NULL,
    username      TEXT UNIQUE NOT NULL,
    password_hash TEXT NOT NULL,
    created_at    TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- The allergens a user has selected from the list.
CREATE TABLE IF NOT EXISTS user_allergens (
    user_id     INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    allergen_id INTEGER NOT NULL REFERENCES allergens(id) ON DELETE CASCADE,
    PRIMARY KEY (user_id, allergen_id)
);

-- Cached foods imported from Open Food Facts.
CREATE TABLE IF NOT EXISTS food_items (
    id               SERIAL PRIMARY KEY,
    off_barcode      TEXT UNIQUE,          -- nullable: leaves room for non-barcode items later
    name             TEXT NOT NULL,
    brand            TEXT,
    category         TEXT,                 -- from OFF categories (optional)
    diet             TEXT,                 -- from OFF ingredients_analysis (vegan/vegetarian, optional)
    ingredients_text TEXT,
    image_url        TEXT,
    last_fetched_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);
-- Full-text index for name search (Phase 2).
CREATE INDEX IF NOT EXISTS idx_food_items_name_fts
    ON food_items USING gin (to_tsvector('english', name));

-- Resolved allergen sets per food (computed once at ingestion).
-- food_contains = 🔴 actually contains; food_traces = 🟡 may contain / traces.
CREATE TABLE IF NOT EXISTS food_contains (
    food_id     INTEGER NOT NULL REFERENCES food_items(id) ON DELETE CASCADE,
    allergen_id INTEGER NOT NULL REFERENCES allergens(id) ON DELETE CASCADE,
    PRIMARY KEY (food_id, allergen_id)
);

CREATE TABLE IF NOT EXISTS food_traces (
    food_id     INTEGER NOT NULL REFERENCES food_items(id) ON DELETE CASCADE,
    allergen_id INTEGER NOT NULL REFERENCES allergens(id) ON DELETE CASCADE,
    PRIMARY KEY (food_id, allergen_id)
);

COMMIT;

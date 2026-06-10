-- Foodi schema — recipes alongside grocery products
-- Apply:  psql -h localhost -p 5433 -U foodi -d foodi -v ON_ERROR_STOP=1 -f migrations/005_recipes.sql
-- Idempotent: safe to re-run.

BEGIN;

-- Distinguish cached groceries (Open Food Facts) from recipes (Spoonacular).
-- Existing rows default to 'grocery'.
ALTER TABLE food_items
    ADD COLUMN IF NOT EXISTS kind TEXT NOT NULL DEFAULT 'grocery';

-- Spoonacular recipe id. Nullable (only recipes have it); off_barcode stays the
-- dedup key for groceries, spoonacular_id for recipes.
ALTER TABLE food_items
    ADD COLUMN IF NOT EXISTS spoonacular_id INTEGER;

-- Partial unique index: lets recipe ingestion upsert on spoonacular_id while
-- leaving grocery rows (NULL spoonacular_id) unconstrained.
CREATE UNIQUE INDEX IF NOT EXISTS idx_food_items_spoonacular
    ON food_items (spoonacular_id) WHERE spoonacular_id IS NOT NULL;

-- Helps the per-kind cache-miss check and the kind filter in search.
CREATE INDEX IF NOT EXISTS idx_food_items_kind ON food_items (kind);

COMMIT;

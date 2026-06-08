-- Foodi migration 003 — add users.profile_completed
-- Distinguishes "hasn't finished onboarding yet" from "finished, but has no
-- allergies" (an empty allergen list is valid). The client reads this to decide
-- whether to route a user to the allergen-setup page or straight to the food list.
-- Apply:  psql -h localhost -p 5433 -U foodi -d foodi -v ON_ERROR_STOP=1 -f migrations/003_add_profile_completed.sql
-- Idempotent: ADD COLUMN IF NOT EXISTS.

ALTER TABLE users ADD COLUMN IF NOT EXISTS profile_completed BOOLEAN NOT NULL DEFAULT false;

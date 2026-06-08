-- Foodi migration 004 — password reset tokens
-- Backs the "forgot password" flow. We store only a HASH of the emailed code
-- (sha256 hex), never the code itself, so a database leak can't be replayed to
-- reset accounts. Each row is single-use (used_at) and time-limited (expires_at),
-- and carries an attempts counter so a code can't be brute-forced online.
-- Apply:  psql -h localhost -p 5433 -U foodi -d foodi -v ON_ERROR_STOP=1 -f migrations/004_password_reset_tokens.sql
-- Idempotent: CREATE TABLE / INDEX IF NOT EXISTS.

BEGIN;

CREATE TABLE IF NOT EXISTS password_reset_tokens (
    id          SERIAL PRIMARY KEY,
    user_id     INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    code_hash   TEXT NOT NULL,                 -- sha256 hex of the normalized code
    expires_at  TIMESTAMPTZ NOT NULL,
    used_at     TIMESTAMPTZ,                   -- non-null once consumed / invalidated
    attempts    INTEGER NOT NULL DEFAULT 0,    -- failed verifications (brute-force cap)
    created_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- We always look up a user's most recent live token, so index by user.
CREATE INDEX IF NOT EXISTS idx_password_reset_tokens_user
    ON password_reset_tokens (user_id);

COMMIT;

# Foodi — Design & Architecture

**Status:** Draft v1
**Date:** 2026-06-06
**Supersedes:** the single-file console app (`Foodi.cpp`); see [`REVIEW.md`](./REVIEW.md) for the review that motivated this rewrite.

Foodi is being rebuilt from a local console program into a **networked food allergen checker**: a Qt desktop client backed by a C++ (Drogon) API server, a PostgreSQL database, and food data sourced from **Open Food Facts**. Users have real accounts, pick their allergens from a standard list, search foods, and immediately see which items are safe for them.

---

## 1. Goals

- **Accounts, not local profiles** — users register/log in and reach their data from any device.
- **Trustworthy allergen matching** — favour a curated allergen knowledge map over naive string matching; never silently miss a derived ingredient.
- **Real food data** — populate foods from Open Food Facts instead of manual entry, cached server-side.
- **Clear verdicts** — every food shows 🟢 Safe / 🔴 Contains / 🟡 May contain, with a "safe only" filter and a detail breakdown.

### Non-goals (for now)
- Mobile/web clients (the API leaves the door open, but the first client is Qt desktop).
- Nutrition tracking, recipes, or meal planning.
- Offline-first sync/conflict resolution on the client.

---

## 2. Architecture

A Qt desktop app must **not** talk to the cloud database directly (that would mean shipping DB credentials in a binary and exposing the DB to the internet). Instead, three tiers:

```
   ┌─────────────────┐     HTTPS / JSON      ┌──────────────────────┐        ┌──────────────┐
   │  Qt Desktop      │  ───── REST ─────►    │  Drogon API server    │ ──────►│  PostgreSQL   │
   │  Client (C++)    │  ◄──── JWT  ─────     │  (C++ : auth, logic)  │ ◄──────│  (cloud DB)   │
   └─────────────────┘                        └──────────┬───────────┘        └──────────────┘
                                                          │  HTTPS (cache miss / refresh)
                                                          ▼
                                                ┌──────────────────────┐
                                                │  Open Food Facts API  │
                                                └──────────────────────┘
```

- The **client** renders UI, holds the user's JWT, and calls the API.
- The **backend** owns all secrets, hashes passwords, issues tokens, runs the allergen matching, and is the *only* thing that calls Open Food Facts (so caching, rate limiting, and the required User-Agent live in one place).
- **PostgreSQL** stores users, allergens, and the cached/normalized food catalogue.

---

## 3. Tech stack

| Layer | Choice | Why |
|-------|--------|-----|
| Client | **Qt 6 Widgets** (C++17) | Native desktop UI; forms + tables fit this app well. |
| Backend | **Drogon** (C++17) | Async REST framework; built-in ORM + JsonCpp; keeps the project one language. |
| Database | **PostgreSQL** | Relational fit for the join-heavy allergen model; strong indexing. |
| Password hashing | **libsodium** (`crypto_pwhash`, Argon2id) | Memory-hard KDF; the correct tool (not a raw hash). |
| Tokens | **jwt-cpp** (header-only) | Stateless auth via signed JWTs, wrapped in a Drogon filter. |
| Food data | **Open Food Facts** REST API | Free, no key, allergen-tagged using the EU taxonomy. |
| Outbound HTTP | **libcurl** (synchronous easy API) | Calls to Open Food Facts. Uses the OS network/TLS/DNS stack — schannel on Windows, system CA on Linux — so it "just works" cross-platform. Drogon's own `HttpClient` is async/event-loop bound and its trantor resolver fails to read Windows DNS config (`BadServerAddress`); since the cache-miss path blocks on the call anyway, libcurl is the simpler and more robust fit. |
| Build / deps | **CMake** + **vcpkg** (or Conan) | Pulls Drogon, libpq, libsodium, jwt-cpp, curl; containerizable. |

---

## 4. Data model (PostgreSQL)

Everything reduces to ~14 canonical allergen IDs, so per-user safety checks become fast set intersections.

```sql
-- Canonical allergen list, seeded with the EU 14 (superset of the US "Big 9").
CREATE TABLE allergens (
    id           SERIAL PRIMARY KEY,
    off_tag      TEXT UNIQUE NOT NULL,   -- e.g. 'en:milk', 'en:gluten'  (matches Open Food Facts)
    display_name TEXT NOT NULL           -- e.g. 'Milk', 'Gluten (cereals)'
);

-- Dictionary fallback for matching raw ingredient text when OFF tags are sparse.
-- e.g. (milk, 'whey'), (milk, 'casein'), (milk, 'lactose'), (gluten, 'semolina')
CREATE TABLE allergen_synonyms (
    id          SERIAL PRIMARY KEY,
    allergen_id INT NOT NULL REFERENCES allergens(id),
    term        TEXT NOT NULL,           -- normalized, lowercase
    UNIQUE (allergen_id, term)
);

CREATE TABLE users (
    id                SERIAL PRIMARY KEY,
    email             TEXT UNIQUE NOT NULL,
    username          TEXT UNIQUE NOT NULL,
    password_hash     TEXT NOT NULL,      -- Argon2id encoded string (includes salt + params)
    profile_completed BOOLEAN NOT NULL DEFAULT false,  -- false until allergen onboarding is finished
    created_at        TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- The allergens a user has selected from the list.
CREATE TABLE user_allergens (
    user_id     INT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    allergen_id INT NOT NULL REFERENCES allergens(id),
    PRIMARY KEY (user_id, allergen_id)
);

-- Cached foods imported from Open Food Facts.
CREATE TABLE food_items (
    id               SERIAL PRIMARY KEY,
    off_barcode      TEXT UNIQUE,        -- Open Food Facts product code
    name             TEXT NOT NULL,
    brand            TEXT,
    category         TEXT,               -- from OFF categories (optional)
    diet             TEXT,               -- from OFF ingredients_analysis (vegan/vegetarian, optional)
    ingredients_text TEXT,
    image_url        TEXT,
    last_fetched_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- 🔴 Hard match: the product actually contains this allergen.
CREATE TABLE food_contains (
    food_id     INT NOT NULL REFERENCES food_items(id) ON DELETE CASCADE,
    allergen_id INT NOT NULL REFERENCES allergens(id),
    PRIMARY KEY (food_id, allergen_id)
);

-- 🟡 Soft match: "may contain" / traces.
CREATE TABLE food_traces (
    food_id     INT NOT NULL REFERENCES food_items(id) ON DELETE CASCADE,
    allergen_id INT NOT NULL REFERENCES allergens(id),
    PRIMARY KEY (food_id, allergen_id)
);

CREATE INDEX idx_food_name ON food_items USING gin (to_tsvector('english', name));
```

---

## 5. Allergen matching — the core algorithm

The expensive work happens **once at ingestion**, not per search.

### 5.1 At ingestion (when a food is imported/refreshed from OFF)
1. Read OFF's structured `allergens_tags` → resolve each to an `allergens.id` → insert into `food_contains`. *(OFF already maps derived ingredients like whey → `en:milk`.)*
2. Read OFF's `traces_tags` → insert into `food_traces`.
3. **Fallback:** if `allergens_tags` is empty/sparse, normalize `ingredients_text` (lowercase, strip punctuation, tokenize, singularize) and match tokens against `allergen_synonyms` (word-boundary aware — **not** raw substring). Any hit → `food_contains`.

> **Why not naive substring?** It fails both ways: false positives (`egg`→egg**plant**, `oat`→g**oat**, `wheat`→buck**wheat**) *and* false negatives — the dangerous dairy terms (whey, casein, lactose, ghee) contain no "milk" substring at all. The curated synonym map + OFF tags catch the derived ingredients while avoiding the false friends.

### 5.2 At query time (per user, per food)
Pure set intersection against the user's selected allergens:

```
contains_hits = user_allergens ∩ food_contains      -- 🔴 "Contains X, Y"
trace_hits    = user_allergens ∩ food_traces        -- 🟡 "May contain Z"

status = contains_hits ? UNSAFE
       : trace_hits    ? CAUTION
       :                 SAFE
```

This is a couple of indexed joins in SQL, so search stays fast even with the whole catalogue loaded.

### 5.3 Allergen taxonomy
Seed `allergens` with the **EU 14 major allergens** (cereals/gluten, crustaceans, eggs, fish, peanuts, soybeans, milk, tree nuts, celery, mustard, sesame, sulphites, lupin, molluscs). This is a superset of the US "Big 9" and matches OFF's `*_tags` 1:1, so the user's picks, the food's data, and the matching vocabulary are all the same set of IDs.

---

## 6. Open Food Facts integration

- **Endpoints:** product by barcode `GET /api/v2/product/{barcode}.json`; text search `GET /api/v2/search?...`.
- **Fields used:** `product_name`, `brands`, `allergens_tags`, `traces_tags`, `ingredients_text`, `ingredients_analysis_tags` (diet), `categories_tags`, `image_url`, `code`.
- **Caching:** on a search/lookup, serve from `food_items` first; on a miss, call OFF, normalize, and upsert. Re-fetch entries older than a TTL (e.g. 30 days via `last_fetched_at`).
- **Etiquette (required):** send a descriptive `User-Agent` (`Foodi/0.1 (contact email)`), respect rate limits, and **attribute** OFF data (it's under the Open Database License / ODbL).

---

## 7. Authentication & security

- **Registration:** hash the password with **Argon2id** (`crypto_pwhash`, at least `INTERACTIVE`/`MODERATE` ops & mem limits). Store only the encoded hash (salt + params embedded).
- **Login:** verify with `crypto_pwhash_str_verify`; on success issue a **signed JWT** (HS256 with a server secret, short expiry, e.g. 1h). Optional refresh token for longer sessions.
- **Authorization:** a Drogon filter validates the `Authorization: Bearer <jwt>` header on protected routes and injects the user id.
- **Transport:** **HTTPS/TLS everywhere** — terminate at a reverse proxy (nginx) or Drogon's built-in SSL.
- **Hardening:** parameterized queries (ORM), input validation, rate-limit auth endpoints (mitigate brute force), secrets via environment/config (never committed), generic auth error messages (don't reveal which field was wrong).
- **Client token storage:** keep the JWT in memory; for "remember me," store it in the OS keychain via **QtKeychain**, not a plaintext file.

---

## 8. REST API (v1)

| Method | Path | Auth | Purpose |
|--------|------|------|---------|
| `POST` | `/api/auth/register` | — | Create account (email, username, password); returns a JWT. New users start `profile_completed = false`. |
| `POST` | `/api/auth/login` | — | Verify credentials; return a JWT + `profile_completed` (client routes to onboarding vs. food list). |
| `GET`  | `/api/allergens` | — | The EU-14 list (for the picker). |
| `GET`  | `/api/me` | ✅ | Current user: username, email, `profile_completed`, selected allergens. |
| `PUT`  | `/api/me` | ✅ | Update profile name (username). |
| `PUT`  | `/api/me/password` | ✅ | Change password (verifies current, re-hashes new). |
| `PUT`  | `/api/me/allergens` | ✅ | Replace allergen selection; marks `profile_completed = true` (the "finish setup" step — an empty list is valid). |
| `GET`  | `/api/foods?q=&category=&diet=&safe_only=&page=` | ✅ | Search with OFF category/diet filters; each result carries `status` + offending allergens for *this* user. |
| `GET`  | `/api/foods/filters` | ✅ | Distinct category & diet values in the catalogue (populates filter dropdowns). |
| `GET`  | `/api/foods/{id}` | ✅ | Detail + per-allergen breakdown (which ingredient/tag triggered it). |

**Onboarding flow (client → API):** register (`POST /auth/register`, get JWT) → allergen-setup page (`GET /allergens`, then `PUT /me/allergens` to save + finish) → food list (`GET /foods` with `GET /foods/filters` for dropdowns) → profile page (`PUT /me`, `PUT /me/password`, `PUT /me/allergens`). On launch, `GET /me`'s `profile_completed` decides setup-page vs. food-list. A future "preferred diet" (vegan, etc.) slots onto the user as another onboarding step — out of scope for now.

Example search result item:
```json
{
  "id": 42,
  "name": "Chocolate Milkshake",
  "brand": "Acme",
  "image_url": "https://...",
  "status": "UNSAFE",
  "contains": ["Milk"],
  "may_contain": ["Tree nuts"]
}
```

---

## 9. Qt client

- **Screens:** Login / Register → Allergen picker (EU-14 checkboxes, editable later in Profile) → Main (search) → Food detail.
- **Search view:** search box + `QListView`/`QTableView` with a custom delegate that draws the colored badge (🟢/🔴/🟡) and the offending allergen names; a **"Safe only"** toggle filters to safe items.
- **Detail view:** image, ingredients, and the per-allergen breakdown.
- **Networking:** `QNetworkAccessManager`; attach `Authorization: Bearer` to every request; parse JSON with `QJsonDocument`.
- **State:** hold the JWT and the user's allergen set in a small session object.

---

## 10. Phased build plan

**Phase 0 — Scaffolding**
Repo layout, CMake + vcpkg manifest, Postgres via Docker for local dev, config/secrets handling.

**Phase 1 — Backend foundation**
Schema + migrations; seed the EU-14 allergens and the synonym dictionary; register/login with Argon2 + JWT; `/api/me` and `/api/me/allergens`. Verify with `curl`.

**Phase 2 — Food data + matching**
OFF import service; normalization into `food_contains` / `food_traces`; the synonym fallback; `/api/foods` search and `/api/foods/{id}` with per-user status.

**Phase 3 — Qt client**
Auth screens, allergen picker, search view with badges + "safe only", detail view.

**Phase 4 — Polish & deploy**
HTTPS, rate limiting, cache TTL/refresh, error handling, packaging the client, deploying the backend + DB.

---

## 11. Proposed repository layout

```
Foodi/
├── backend/            # Drogon API server
│   ├── CMakeLists.txt
│   ├── vcpkg.json
│   ├── config.json     # (gitignored; secrets via env)
│   ├── migrations/     # SQL schema + seed (EU-14, synonyms)
│   └── src/
│       ├── controllers/  (auth, foods, me)
│       ├── filters/      (jwt auth)
│       ├── services/     (off_client, allergen_matcher)
│       └── models/       (ORM-generated)
├── client/             # Qt desktop app
│   ├── CMakeLists.txt
│   └── src/            (ui/, net/, session)
├── DESIGN.md
└── REVIEW.md
```

---

## 12. Open questions / future

- **Hosting** — which provider for the backend + managed Postgres (later decision).
- **Synonym dictionary scope** — start small (the high-risk derived ingredients) and grow it as gaps appear.
- **Free-text foods** — OFF is product/barcode oriented; do we also want generic dishes (e.g. "pizza") later? Could add a second source or manual entries.
- **Refresh tokens / "remember me"** — confirm session length expectations.
- **Multi-client** — the REST API already allows a future web/mobile client with no backend changes.

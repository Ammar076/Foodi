# Foodi

A personal food-allergen checker. Search groceries and recipes, see exactly which of your allergens each one contains or may contain, and get a clear safe / caution / unsafe verdict — backed by live Open Food Facts (groceries) and Spoonacular (recipes) data.

The original single-file console app (2023/2024) lives in [`old version/`](old%20version/README.md). This is the full rebuild: a Qt 6 desktop client talking to a Drogon C++ REST backend with a PostgreSQL database.

---

## Try it

- **Download the app:** grab `Foodi-windows-x64.zip` from the [Releases](https://github.com/Ammar076/Foodi/releases) page, unzip anywhere, and run `foodi_client.exe`. No install needed — the Qt runtime is bundled, and it talks to the hosted backend out of the box.
- **Heads up — cold start:** the backend is hosted on a free tier that sleeps after ~15 min idle, so the *first action after a pause can take 30–60s* while the server wakes. This is expected, not a crash.

Want to run the whole stack yourself instead? See [Setup](#setup) below.

---

## Features

- **Allergen profiles** — pick from the EU-14 canonical allergens; saved to your account so any device can use them
- **Groceries and recipes** — search Open Food Facts for packaged products or Spoonacular for recipes, with an All / Groceries / Recipes toggle and "Load more" paging; results are cached server-side so repeat searches are instant
- **Three-tier verdict** — Unsafe (contains your allergen) / Caution (traces only) / Safe, shown per item and per allergen; recipe gluten is inferred from the ingredient list (flour-style ingredients flag gluten, gluten-free flours don't)
- **Light / Dark / System theme** — switch in Profile; System follows your OS appearance
- **Forgot password** — real email reset via SMTP; tokens are single-use, time-limited, and brute-force capped
- **Remember me** — stay logged in across launches via a stored token

## Tech stack

| Layer | Technology |
| --- | --- |
| Desktop client | Qt 6.11 Widgets (C++, MSVC) |
| REST backend | Drogon C++ |
| Database | PostgreSQL 16 (Docker) |
| Auth | Argon2id (libsodium) + JWT HS256 |
| Grocery data | Open Food Facts (no API key needed) |
| Recipe data | Spoonacular (free API key) |
| Email | libcurl SMTP (STARTTLS) |
| Hosting (free) | Neon (Postgres) + Render (Docker web service) |

---

## Prerequisites

- **Windows 10/11** with VS 2022 Community ("Desktop development with C++" workload)
- **Docker Desktop**
- **Qt 6.11.1** — MSVC 2022 64-bit kit (install via the [Qt online installer](https://www.qt.io/download-qt-installer); tick *Qt 6.x → MSVC 2022 64-bit*)
- **vcpkg** at `C:\dev\vcpkg` (instructions below)
- **Spoonacular API key** — free at [spoonacular.com/food-api](https://spoonacular.com/food-api) (only needed for recipe search; grocery search works without it)
- **Git**

---

## Setup

### 1. Clone and bootstrap vcpkg

```powershell
git clone https://github.com/Ammar076/Foodi C:\Code\Foodi
git clone https://github.com/microsoft/vcpkg C:\dev\vcpkg
C:\dev\vcpkg\bootstrap-vcpkg.bat
[Environment]::SetEnvironmentVariable("VCPKG_ROOT", "C:\dev\vcpkg", "User")
# open a new terminal so VCPKG_ROOT takes effect
```

### 2. Start the database

```powershell
cd C:\Code\Foodi
docker compose up -d
```

The container runs on host port **5433** (not 5432) to avoid clashing with any local PostgreSQL install. Verify:

```powershell
$env:PGPASSWORD = "foodi_dev_password"
psql -h localhost -p 5433 -U foodi -d foodi -c "select version();"
```

### 3. Apply migrations

```powershell
foreach ($f in Get-ChildItem backend\migrations\*.sql | Sort-Object Name) {
    psql -h localhost -p 5433 -U foodi -d foodi -f $f.FullName
}
```

### 4. Configure the backend

```powershell
Copy-Item backend\config.example.json backend\config.json
```

Open `backend\config.json` and set a real value for `jwt_secret`. For recipe search, paste your key into `spoonacular_api_key` (or leave it as the placeholder and only grocery search will be enabled). Everything else works out of the box for local dev. To enable password-reset emails, fill in the `smtp_*` fields and set `smtp_enabled` to `true`, then set the password:

```powershell
$env:FOODI_SMTP_PASSWORD = "your-app-password"
```

### 5. Build the backend

All build commands must run from the **Developer PowerShell for VS 2022** (Start menu, or Terminal → new "Developer PowerShell" profile inside VS Code):

```powershell
cd C:\Code\Foodi\backend
$env:VCPKG_ROOT = "C:\dev\vcpkg"   # the VS dev shell overrides this; reset it
cmake --preset default              # first run takes 15–40 min (vcpkg compiles deps)
cmake --build build
.\build\foodi_backend.exe
```

Verify:

```powershell
curl http://localhost:8080/health
# {"status":"ok","service":"foodi-backend"}
```

### 6. Build and run the client

```powershell
cd C:\Code\Foodi\client
cmake --preset default
cmake --build build
.\build\foodi_client.exe
```

The client connects to `http://127.0.0.1:8080` by default. If your Qt kit is installed somewhere other than `C:/Qt/6.11.1/msvc2022_64`, update `CMAKE_PREFIX_PATH` in `client/CMakePresets.json`.

To point the client at a different backend, bake the URL in at configure time with `-DFOODI_API_URL="https://your-host"`, or override it at runtime by setting the `FOODI_API_URL` environment variable before launching — no rebuild needed.

---

## Project layout

```
Foodi/
├── docker-compose.yml          # dev Postgres on port 5433
├── backend/                    # Drogon REST API
│   ├── CMakeLists.txt
│   ├── vcpkg.json              # drogon, libsodium, jwt-cpp, libcurl
│   ├── config.example.json     # copy → config.json (git-ignored)
│   ├── Dockerfile              # multi-stage Linux build (vcpkg)
│   ├── deploy/                 # non-secret config.docker.json for hosting
│   ├── migrations/             # 001 schema · 002 allergens · 003–004 features · 005 recipes
│   └── src/
│       ├── controllers/        # Auth · Me · Allergen · Food
│       ├── filters/            # JwtAuthFilter
│       ├── services/           # OffClient · SpoonacularClient · FoodIngest · EmailClient
│       └── util/               # Jwt · PasswordHasher · Responses · Strings
├── client/                     # Qt 6 desktop app
│   ├── CMakeLists.txt
│   ├── resources/              # foodi.qss (tokenized theme) + check icons
│   └── src/
│       ├── core/               # Session (shared state)
│       ├── net/                # ApiClient (QNetworkAccessManager)
│       └── ui/                 # all screens and widgets (Theme = light/dark/system)
└── old version/                # original 2023/2024 console app
```

---

## API overview

All endpoints are at `http://localhost:8080`.

| Method | Path | Auth | Description |
| --- | --- | --- | --- |
| POST | `/api/auth/register` | — | Create account |
| POST | `/api/auth/login` | — | Login, receive JWT |
| POST | `/api/auth/forgot-password` | — | Request reset code by email |
| POST | `/api/auth/reset-password` | — | Submit code + new password |
| GET | `/api/me` | JWT | Get profile |
| PUT | `/api/me` | JWT | Update display name |
| PUT | `/api/me/password` | JWT | Change password |
| PUT | `/api/me/allergens` | JWT | Save allergen list |
| GET | `/api/allergens` | JWT | List all 14 allergens |
| GET | `/api/foods?q=&source=&page=&pageSize=` | JWT | Search foods. `source` = `all` \| `grocery` (Open Food Facts) \| `recipe` (Spoonacular); paginated, returns `has_more` |
| GET | `/api/foods/filters` | JWT | Available categories and diets |
| GET | `/api/foods/:id` | JWT | Food detail with per-user verdict |

The server fills its cache on demand: a search miss fetches from Open Food Facts (groceries) or Spoonacular (recipes), ingests the results, and serves cached rows on subsequent queries.

---

## Deployment

The hosted build runs entirely on free tiers: **Neon** for PostgreSQL and **Render** for the backend (built from `backend/Dockerfile`, auto-HTTPS). The release client is then built with the Render URL baked in and published as a GitHub release asset.

The backend is fully configurable by environment variables (no secrets in the repo or image) — `config.json` is an optional local-dev fallback:

| Variable | Purpose |
| --- | --- |
| `FOODI_DB_HOST` / `FOODI_DB_PORT` / `FOODI_DB_NAME` / `FOODI_DB_USER` / `FOODI_DB_PASSWORD` | Database connection |
| `PGSSLMODE=require` | TLS to a managed Postgres (e.g. Neon) |
| `FOODI_JWT_SECRET` | JWT signing secret |
| `FOODI_SPOONACULAR_KEY` | Spoonacular API key (recipes) |
| `PORT` | Listen port (injected by the host; defaults to 8080) |
| `FOODI_SMTP_*` | Optional password-reset email |

Build and run the image locally:

```powershell
cd C:\Code\Foodi\backend
docker build -t foodi-backend .
docker run -p 8080:8080 --env-file prod.env foodi-backend
```

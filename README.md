# Foodi

A personal food-allergen checker. Search any food, see exactly which of your allergens it contains or may contain, and get a clear safe / caution / unsafe verdict — all backed by live Open Food Facts data.

The original single-file console app (2023/2024) lives in [`old version/`](old%20version/README.md). This is the full rebuild: a Qt 6 desktop client talking to a Drogon C++ REST backend with a PostgreSQL database.

---

## Features

- **Allergen profiles** — pick from the EU-14 canonical allergens; saved to your account so any device can use them
- **Live food search** — queries Open Food Facts, cached server-side so repeat searches are instant
- **Three-tier verdict** — Unsafe (contains your allergen) / Caution (traces only) / Safe, shown per product and per allergen
- **Forgot password** — real email reset via SMTP; tokens are single-use, time-limited, and brute-force capped
- **Remember me** — stay logged in across launches via a stored token

## Tech stack

| Layer | Technology |
| --- | --- |
| Desktop client | Qt 6.11 Widgets (C++, MSVC) |
| REST backend | Drogon C++ |
| Database | PostgreSQL 16 (Docker) |
| Auth | Argon2id (libsodium) + JWT HS256 |
| Food data | Open Food Facts (no API key needed) |
| Email | libcurl SMTP (STARTTLS) |

---

## Prerequisites

- **Windows 10/11** with VS 2022 Community ("Desktop development with C++" workload)
- **Docker Desktop**
- **Qt 6.11.1** — MSVC 2022 64-bit kit (install via the [Qt online installer](https://www.qt.io/download-qt-installer); tick *Qt 6.x → MSVC 2022 64-bit*)
- **vcpkg** at `C:\dev\vcpkg` (instructions below)
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

Open `backend\config.json` and set a real value for `jwt_secret`. Everything else works out of the box for local dev. To enable password-reset emails, fill in the `smtp_*` fields and set `smtp_enabled` to `true`, then set the password:

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

---

## Project layout

```
Foodi/
├── docker-compose.yml          # dev Postgres on port 5433
├── backend/                    # Drogon REST API
│   ├── CMakeLists.txt
│   ├── vcpkg.json              # drogon, libsodium, jwt-cpp, libcurl
│   ├── config.example.json     # copy → config.json (git-ignored)
│   ├── migrations/             # 001 schema · 002 allergens · 003–004 features
│   └── src/
│       ├── controllers/        # Auth · Me · Allergen · Food
│       ├── filters/            # JwtAuthFilter
│       ├── services/           # OffClient · FoodIngest · EmailClient
│       └── util/               # Jwt · PasswordHasher · Responses · Strings
├── client/                     # Qt 6 desktop app
│   ├── CMakeLists.txt
│   ├── resources/              # foodi.qss theme + check icons
│   └── src/
│       ├── core/               # Session (shared state)
│       ├── net/                # ApiClient (QNetworkAccessManager)
│       └── ui/                 # all screens and widgets
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
| GET | `/api/foods?q=...` | JWT | Search foods (cache → OFF fallback) |
| GET | `/api/foods/filters` | JWT | Available categories and diets |
| GET | `/api/foods/:id` | JWT | Food detail with per-user verdict |

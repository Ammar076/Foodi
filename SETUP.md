# Foodi — Local Setup (Phase 0)

This gets the dev environment ready and builds the Phase 0 backend skeleton.
See [`DESIGN.md`](./DESIGN.md) for the architecture and [`REVIEW.md`](./REVIEW.md) for the original code review.

## What you already have ✅
- Docker + Compose
- Git
- PostgreSQL 18 + `psql` (local)
- **MSVC C++ toolchain** (`cl.exe`) via VS 2022's "Desktop development with C++" workload — *confirmed installed*
- **VS Code** with the **C/C++** and **CMake Tools** extensions — this stays your editor; the VS IDE is not needed

## What you need to do manually

### 1. Bootstrap vcpkg
Pick a location (e.g. `C:\dev\vcpkg`) and run, in a normal PowerShell:
```powershell
git clone https://github.com/microsoft/vcpkg C:\dev\vcpkg
C:\dev\vcpkg\bootstrap-vcpkg.bat
```
Then set `VCPKG_ROOT` permanently (so CMakePresets can find the toolchain):
```powershell
[Environment]::SetEnvironmentVariable("VCPKG_ROOT", "C:\dev\vcpkg", "User")
```
**Open a new terminal afterwards** so the variable takes effect.

### 2. Start the database
From the project root:
```powershell
docker compose up -d
```
Connect to verify (password is `foodi_dev_password`):
```powershell
$env:PGPASSWORD = "foodi_dev_password"
psql -h localhost -p 5433 -U foodi -d foodi -c "select version();"
```
> Note: the container is on host port **5433** so it won't clash with your local PostgreSQL 18 on 5432. Stop it later with `docker compose down` (add `-v` to also wipe the data volume).

### 3. Create your local config
```powershell
Copy-Item backend\config.example.json backend\config.json
```
Edit `backend\config.json` and set a real `jwt_secret`. This file is git-ignored (it holds secrets).

### 4. Build the backend
The first build downloads and compiles Drogon + dependencies via vcpkg — **expect 15–40 minutes the first time** (cached afterwards). Two ways, both keeping VS Code as your editor:

**Option A — command line (simplest, most reliable).** Build from the **"Developer PowerShell for VS 2022"** (Start menu) so CMake/Ninja/`cl` are on PATH. You can open this terminal *inside* VS Code: Terminal → New Terminal → pick the "Developer PowerShell" profile.
```powershell
cd C:\Code\Foodi\backend
$env:VCPKG_ROOT = "C:\dev\vcpkg"   # the VS dev shell overrides this with its bundled vcpkg; reset it
cmake --preset default
cmake --build build
.\build\foodi_backend.exe
```
> Why the `VCPKG_ROOT` line: the VS Developer PowerShell sets `VCPKG_ROOT` to Visual Studio's *bundled* vcpkg, which doesn't match our pinned baseline. Resetting it to `C:\dev\vcpkg` makes the build use your vcpkg. (`vcpkg.json` pins `builtin-baseline` to that instance.)

**Option B — VS Code CMake Tools.** Open the `backend/` folder; CMake Tools detects `CMakePresets.json`. Select the **default** configure preset (bottom status bar). If prompted for a kit, choose **Visual Studio Community 2022 - amd64**. Then Configure → Build → Run. (Requires `VCPKG_ROOT` to be set first, per step 1.)

Verify in another terminal:
```powershell
curl http://localhost:8080/health
# -> {"status":"ok","service":"foodi-backend","phase":0}
```

### 5. Qt 6 — desktop client (Phase 3)
Install Qt 6 via the [online installer](https://www.qt.io/download-qt-installer) and, in the
component tree under **Qt 6.x**, tick **MSVC 2022 64-bit** (it must match the MSVC toolchain the
backend uses — MinGW builds are ABI-incompatible). Qt Widgets + Qt Network come with that
component automatically; no extra modules are needed.

Build the client (from the **Developer PowerShell for VS 2022**, same as the backend):
```powershell
cd C:\Code\Foodi\client
cmake --preset default       # uses CMAKE_PREFIX_PATH from CMakePresets.json
cmake --build build
.\build\foodi_client.exe     # windeployqt copies the Qt DLLs next to it during build
```
> This repo's `CMAKE_PREFIX_PATH` is set to `C:/dev/Qt/6.11.1/msvc2022_64` (where Qt is
> installed on this machine). If your kit differs, edit it in `client/CMakePresets.json`
> (or pass `-DCMAKE_PREFIX_PATH=<your kit>` to the configure step).
> The client expects the backend running at `http://127.0.0.1:8080` (see `Session::baseUrl`).

---

## Project layout
```
Foodi/
├── docker-compose.yml      # dev Postgres (host port 5433)
├── backend/                # Drogon API server
│   ├── CMakeLists.txt
│   ├── CMakePresets.json   # "default" preset wires in the vcpkg toolchain
│   ├── vcpkg.json          # drogon[ctl,postgres], libsodium, jwt-cpp
│   ├── config.example.json # copy -> config.json (git-ignored)
│   ├── migrations/         # SQL schema + seed (Phase 1)
│   └── src/                # main.cpp + controllers/filters/services/models
└── client/                 # Qt desktop app (Phase 3)
```

## Definition of done for Phase 0
- [ ] vcpkg bootstrapped, `VCPKG_ROOT` set
- [ ] `docker compose up -d` → database healthy, `psql` connects
- [ ] `backend/config.json` created
- [ ] `foodi_backend` builds and `GET /health` returns ok

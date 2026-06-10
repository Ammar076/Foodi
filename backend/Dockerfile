# Foodi backend — multi-stage Linux build.
#
# Build (from backend/):   docker build -t foodi-backend .
# Run:                     docker run -p 8080:8080 --env-file prod.env foodi-backend
#
# The first build compiles Drogon + deps via vcpkg and takes a while (and a couple
# of GB of RAM). For free PaaS hosts with tight build limits, build locally and push
# the image to a registry, then deploy the image (see DEPLOY.md).

# ---------- build stage ----------
FROM ubuntu:24.04 AS build
ENV DEBIAN_FRONTEND=noninteractive
# build-essential/cmake/ninja/git + the tools vcpkg ports need to build from source:
# perl (OpenSSL), autoconf/automake/autoconf-archive/libtool (autotools ports),
# bison/flex (libpq/Postgres), python3, gperf, pkg-config.
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build git curl zip unzip tar pkg-config \
        ca-certificates linux-libc-dev \
        perl autoconf automake autoconf-archive libtool bison flex python3 gperf \
    && rm -rf /var/lib/apt/lists/*

# vcpkg (manifest mode picks up the pinned baseline in vcpkg.json)
ENV VCPKG_ROOT=/opt/vcpkg
RUN git clone https://github.com/microsoft/vcpkg "$VCPKG_ROOT" \
    && "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics

WORKDIR /src
# Copy the manifest first so the (slow) dependency install layer caches across
# source-only changes.
COPY vcpkg.json .
COPY CMakeLists.txt .
COPY src ./src
COPY migrations ./migrations
COPY config.example.json ./config.example.json

RUN cmake -S . -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
    && cmake --build build

# ---------- runtime stage ----------
FROM ubuntu:24.04 AS runtime
ENV DEBIAN_FRONTEND=noninteractive
# ca-certificates: TLS to Open Food Facts / Spoonacular / SMTP.
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=build /src/build/foodi_backend /app/foodi_backend
COPY --from=build /src/migrations /app/migrations
COPY deploy/config.docker.json /app/config.json

# PaaS platforms inject PORT; default 8080 for plain `docker run`.
ENV PORT=8080
EXPOSE 8080
CMD ["/app/foodi_backend"]

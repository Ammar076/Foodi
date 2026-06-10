cmake_minimum_required(VERSION 3.21)
project(foodi_backend CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Dependencies (provided by vcpkg via the toolchain file / CMakePresets)
find_package(Drogon CONFIG REQUIRED)
find_package(unofficial-sodium CONFIG REQUIRED)   # libsodium -> Argon2 password hashing
find_package(jwt-cpp CONFIG REQUIRED)             # JWT auth tokens
find_package(CURL CONFIG REQUIRED)                # libcurl -> outbound HTTP to Open Food Facts

add_executable(foodi_backend
    src/main.cpp
    src/util/Jwt.cpp
    src/util/PasswordHasher.cpp
    src/filters/JwtAuthFilter.cpp
    src/controllers/AuthController.cpp
    src/controllers/AllergenController.cpp
    src/controllers/MeController.cpp
    src/controllers/FoodController.cpp
    src/services/OffClient.cpp
    src/services/SpoonacularClient.cpp
    src/services/FoodIngest.cpp
    src/services/EmailClient.cpp
)
target_include_directories(foodi_backend PRIVATE src)

# Stop <windows.h> from defining min()/max() macros that clobber std::min/std::max.
target_compile_definitions(foodi_backend PRIVATE NOMINMAX)

target_link_libraries(foodi_backend PRIVATE
    Drogon::Drogon
    unofficial-sodium::sodium
    jwt-cpp::jwt-cpp
    CURL::libcurl
)

# Copy the config next to the binary so the server can find it when run from build/
add_custom_command(TARGET foodi_backend POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config.example.json"
        "$<TARGET_FILE_DIR:foodi_backend>/config.example.json"
)

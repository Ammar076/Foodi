cmake_minimum_required(VERSION 3.21)
project(foodi_client CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt code generators: AUTOMOC handles Q_OBJECT (signals/slots), AUTOUIC/AUTORCC
# handle .ui / .qrc files (none yet, harmless to leave on).
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Qt is found via CMAKE_PREFIX_PATH (see CMakePresets.json -> your Qt msvc2022_64 dir).
find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

add_executable(foodi_client
    src/main.cpp
    src/net/ApiClient.cpp
    src/ui/LoginWindow.cpp
    src/ui/MainWindow.cpp
    src/ui/OnboardingPage.cpp
    src/ui/FoodListPage.cpp
    src/ui/FoodDetailDialog.cpp
    src/ui/AllergenPicker.cpp
    src/ui/ProfilePage.cpp
    src/ui/Card.cpp
    src/ui/Theme.cpp
    src/ui/Spinner.cpp
    resources/foodi.qrc
)
target_include_directories(foodi_client PRIVATE src)
target_link_libraries(foodi_client PRIVATE Qt6::Widgets Qt6::Network)

# On Windows, copy the needed Qt runtime DLLs next to the .exe so it launches
# without Qt being on PATH (windeployqt ships with the Qt kit).
if (WIN32)
    get_target_property(_qmake Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qtbin "${_qmake}" DIRECTORY)
    find_program(WINDEPLOYQT_EXE windeployqt HINTS "${_qtbin}")
    if (WINDEPLOYQT_EXE)
        add_custom_command(TARGET foodi_client POST_BUILD
            COMMAND "${WINDEPLOYQT_EXE}" --no-translations "$<TARGET_FILE:foodi_client>"
            COMMENT "Deploying Qt runtime DLLs next to foodi_client.exe")
    endif()
endif()

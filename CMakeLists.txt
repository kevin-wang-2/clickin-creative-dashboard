cmake_minimum_required(VERSION 3.25)

project(ClickinCreativeDashboard
    VERSION 0.1.0
    DESCRIPTION "Modular asset browser"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDE integration
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Qt ────────────────────────────────────────────────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Multimedia
)
qt_standard_project_setup()

# ── SQLite (vendored amalgamation) ────────────────────────────────────────────
# third_party/sqlite/ contains sqlite3.h + sqlite3.c — no package manager needed.
add_subdirectory(third_party/sqlite)

# ── GoogleTest ────────────────────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# ── Subdirectories ────────────────────────────────────────────────────────────
add_subdirectory(src)
add_subdirectory(tests)

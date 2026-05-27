add_library(sqlite3_lib STATIC sqlite3.c)

target_include_directories(sqlite3_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(sqlite3_lib PRIVATE
    SQLITE_THREADSAFE=2
    SQLITE_DEFAULT_WAL_SYNCHRONOUS=1
    # Suppress MSVC warnings on Windows
    $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
)

set_target_properties(sqlite3_lib PROPERTIES POSITION_INDEPENDENT_CODE ON)

# SQLite is C, not C++ — don't impose C++20 on it
set_target_properties(sqlite3_lib PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)

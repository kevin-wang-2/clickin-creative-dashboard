add_subdirectory(core)
add_subdirectory(sdk)
add_subdirectory(providers)
add_subdirectory(ui)

# ── Application target ────────────────────────────────────────────────────────
qt_add_executable(ClickinDashboard
    main.cpp
)

target_link_libraries(ClickinDashboard PRIVATE
    clickin_core
    clickin_providers
    clickin_ui_shell
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

set_target_properties(ClickinDashboard PROPERTIES
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

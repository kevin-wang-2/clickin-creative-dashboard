qt_add_library(clickin_ui_job_status STATIC
    JobStatusBar.cpp
    JobStatusBar.h
)
target_link_libraries(clickin_ui_job_status PUBLIC clickin_core Qt6::Widgets)

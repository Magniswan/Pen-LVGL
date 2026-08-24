lvgl_add_application(lvgl_sdk_example
    APP_ID top.example.hello
    OUTPUT_NAME lvgl-example
    SOURCES main.cpp
    INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}"
)

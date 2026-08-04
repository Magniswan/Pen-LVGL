cmake_minimum_required(VERSION 3.20)

project(lvgl_dictpen_poc VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_SKIP_RPATH TRUE)

set(LV_BUILD_CONF_PATH "${CMAKE_SOURCE_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/lvgl EXCLUDE_FROM_ALL)

add_library(device_profile STATIC
    src/platform/device_profile/device_profile.cpp
)
target_include_directories(device_profile PUBLIC src)
target_compile_options(device_profile PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(dictpen_probe
    src/probe_main.cpp
)
target_link_libraries(dictpen_probe PRIVATE device_profile)
target_compile_options(dictpen_probe PRIVATE -Wall -Wextra -Wpedantic -Werror)

install(TARGETS dictpen_probe RUNTIME DESTINATION bin)

add_executable(evdev_inject
    src/tools/evdev_inject.cpp
)
target_link_libraries(evdev_inject PRIVATE device_profile)
target_compile_options(evdev_inject PRIVATE -Wall -Wextra -Wpedantic -Werror)
install(TARGETS evdev_inject RUNTIME DESTINATION bin)

add_executable(uinput_sequence
    src/tools/uinput_sequence.cpp
)
target_link_libraries(uinput_sequence PRIVATE device_profile)
target_compile_options(uinput_sequence PRIVATE -Wall -Wextra -Wpedantic -Werror)
install(TARGETS uinput_sequence RUNTIME DESTINATION bin)

set(DEVICE_LIBDRM_PATH
    "${CMAKE_SOURCE_DIR}/device-sysroot/usr/lib/libdrm.so.2.4.0"
    CACHE FILEPATH "Target device libdrm shared object")

if(EXISTS "${DEVICE_LIBDRM_PATH}")
    add_library(device_libdrm SHARED IMPORTED GLOBAL)
    set_target_properties(device_libdrm PROPERTIES
        IMPORTED_LOCATION "${DEVICE_LIBDRM_PATH}"
        IMPORTED_SONAME "libdrm.so.2"
    )

    add_executable(drm_probe
        src/platform/drm/drm_probe.cpp
    )
    target_include_directories(drm_probe PRIVATE
        third_party/libdrm
        third_party/libdrm/include
        third_party/libdrm/include/drm
    )
    target_link_libraries(drm_probe PRIVATE device_libdrm)
    target_compile_options(drm_probe PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-pedantic)

    add_executable(drm_color_test
        src/drm_color_test.cpp
        src/platform/drm/drm_backend.cpp
    )
    target_include_directories(drm_color_test PRIVATE
        src
        third_party/libdrm
        third_party/libdrm/include
        third_party/libdrm/include/drm
    )
    target_link_libraries(drm_color_test PRIVATE device_profile device_libdrm)
    target_compile_options(drm_color_test PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-pedantic)

    add_executable(lvgl_poc
        src/lvgl_poc.cpp
        src/app/poc_ui.cpp
        src/assets/lv_font_poc_16.c
        src/diagnostics/runtime_metrics.cpp
        src/platform/drm/drm_backend.cpp
        src/platform/input/input_backend.cpp
    )
    target_include_directories(lvgl_poc PRIVATE
        src
        third_party/libdrm
        third_party/libdrm/include
        third_party/libdrm/include/drm
    )
    target_link_libraries(lvgl_poc PRIVATE
        device_profile
        device_libdrm
        lvgl::lvgl
    )
    target_compile_options(lvgl_poc PRIVATE
        -Wall -Wextra -Wpedantic -Werror -Wno-pedantic
    )

    install(TARGETS drm_probe drm_color_test lvgl_poc RUNTIME DESTINATION bin)
endif()

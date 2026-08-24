include_guard(GLOBAL)

function(lvgl_add_application target)
    set(options INSTALL)
    set(oneValueArgs APP_ID OUTPUT_NAME)
    set(multiValueArgs SOURCES INCLUDE_DIRECTORIES LIBRARIES)
    cmake_parse_arguments(LVAPP "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT LVAPP_APP_ID MATCHES "^[a-z0-9]+([.-][a-z0-9]+)+$")
        message(FATAL_ERROR "${target}: APP_ID must be a lowercase reverse-domain identifier")
    endif()
    if(NOT LVAPP_OUTPUT_NAME OR NOT LVAPP_SOURCES)
        message(FATAL_ERROR "${target}: OUTPUT_NAME and at least one source are required")
    endif()
    if(NOT TARGET platform_runtime OR NOT TARGET app_shell)
        message(FATAL_ERROR "${target}: platform_runtime and app_shell must be defined first")
    endif()

    add_executable(${target} ${LVAPP_SOURCES})
    set_target_properties(${target} PROPERTIES OUTPUT_NAME "${LVAPP_OUTPUT_NAME}")
    target_include_directories(${target} PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
        ${LVAPP_INCLUDE_DIRECTORIES}
    )
    target_link_libraries(${target} PRIVATE
        platform_runtime
        app_shell
        ${LVAPP_LIBRARIES}
    )
    target_compile_definitions(${target} PRIVATE
        LVGL_APPLICATION_ID="${LVAPP_APP_ID}"
    )
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wpedantic -Werror -Wno-pedantic
    )
    if(LVAPP_INSTALL)
        install(TARGETS ${target} RUNTIME DESTINATION bin)
    endif()
endfunction()

cmake_minimum_required(VERSION 3.20)

project(lvgl_dictpen_poc VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)
include(cmake/LvglApplication.cmake)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_SKIP_RPATH TRUE)

set(LVGL_PLATFORM_OFFICIAL_PUBLIC_KEY_HEX "" CACHE STRING
    "Raw 32-byte Ed25519 official release public key as 64 lowercase hex characters")
option(LVGL_PLATFORM_PRODUCTION_BUILD
    "Require an official release public key and enable production release safeguards" OFF)
option(LVGL_BUILD_SDK_TEMPLATE "Build the SDK example application" ON)
option(LVGL_ENABLE_SANITIZERS "Instrument host targets with AddressSanitizer and UBSan" OFF)
option(LVGL_BUILD_FUZZERS "Build Clang libFuzzer parser targets" OFF)
if(LVGL_BUILD_FUZZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "LVGL_BUILD_FUZZERS requires Clang with libFuzzer")
    endif()
    set(LVGL_ENABLE_SANITIZERS ON CACHE BOOL
        "Instrument host targets with AddressSanitizer and UBSan" FORCE)
endif()
if(LVGL_ENABLE_SANITIZERS)
    if(MSVC OR NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        message(FATAL_ERROR "LVGL_ENABLE_SANITIZERS requires a Clang or GCC host build")
    endif()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()
if(LVGL_PLATFORM_OFFICIAL_PUBLIC_KEY_HEX)
    string(LENGTH "${LVGL_PLATFORM_OFFICIAL_PUBLIC_KEY_HEX}" LVGL_PLATFORM_OFFICIAL_KEY_LENGTH)
    if(NOT LVGL_PLATFORM_OFFICIAL_KEY_LENGTH EQUAL 64 OR
       NOT LVGL_PLATFORM_OFFICIAL_PUBLIC_KEY_HEX MATCHES "^[0-9a-f]+$")
        message(FATAL_ERROR
            "LVGL_PLATFORM_OFFICIAL_PUBLIC_KEY_HEX must be exactly 64 lowercase hex characters")
    endif()
endif()
if(LVGL_PLATFORM_OFFICIAL_PUBLIC_KEY_HEX STREQUAL
   "0000000000000000000000000000000000000000000000000000000000000000")
    message(FATAL_ERROR "The all-zero Ed25519 public key is forbidden")
endif()
if(LVGL_PLATFORM_OFFICIAL_PUBLIC_KEY_HEX STREQUAL
   "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a")
    message(FATAL_ERROR "RFC 8032 public test keys are forbidden in the official trust slot")
endif()
if(LVGL_PLATFORM_PRODUCTION_BUILD AND NOT LVGL_PLATFORM_OFFICIAL_PUBLIC_KEY_HEX)
    message(FATAL_ERROR
        "Production builds require LVGL_PLATFORM_OFFICIAL_PUBLIC_KEY_HEX; private keys must never be supplied here")
endif()
configure_file(
    platform/include/lvgl_platform/official_key_config.h.in
    generated/lvgl_platform/official_key_config.h
    @ONLY
)

set(LV_BUILD_CONF_PATH "${CMAKE_SOURCE_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/lvgl EXCLUDE_FROM_ALL)

add_library(lvgl_sdk_headers INTERFACE)
add_library(lvgl_platform::sdk_headers ALIAS lvgl_sdk_headers)
target_include_directories(lvgl_sdk_headers INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(lvgl_sdk_headers INTERFACE lvgl::lvgl)

add_library(lvgl_platform_contract STATIC
    platform/src/crypto/crypto_provider.cpp
    platform/src/ipc/installer_protocol.cpp
    platform/src/ipc/touch_protocol.cpp
    platform/src/ipc/storage_protocol.cpp
    platform/src/package/rollback_policy.cpp
    platform/src/package/package_installer.cpp
    platform/src/package/package_verifier.cpp
    platform/src/profile/device_profile.cpp
    platform/src/services/inbox_service.cpp
    platform/src/session/application_registry.cpp
    platform/src/session/session_control.cpp
    platform/src/session/session_profile.cpp
    platform/src/session/session_status.cpp
    platform/src/session/touch_router.cpp
    platform/src/storage/release_state.cpp
    platform/src/storage/safe_path.cpp
    platform/src/storage/state_store.cpp
    platform/src/trust/trust_store.cpp
)
target_include_directories(lvgl_platform_contract PUBLIC
    platform/include
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
target_include_directories(lvgl_platform_contract PRIVATE
    third_party/lvgl/src/libs/thorvg
)
target_link_libraries(lvgl_platform_contract PUBLIC ${CMAKE_DL_LIBS})
if(MSVC)
    target_compile_options(lvgl_platform_contract PRIVATE /W4 /WX)
else()
    target_compile_options(lvgl_platform_contract PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
endif()

if(LVGL_BUILD_FUZZERS)
    add_executable(generate_fuzz_corpus tests/fuzz/generate_fuzz_corpus.cpp)
    target_link_libraries(generate_fuzz_corpus PRIVATE lvgl_platform_contract)
    target_compile_options(generate_fuzz_corpus PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_executable(fuzz_package_verifier tests/fuzz/fuzz_package_verifier.cpp)
    add_executable(fuzz_release_state tests/fuzz/fuzz_release_state.cpp)
    add_executable(fuzz_session_protocol tests/fuzz/fuzz_session_protocol.cpp)
    foreach(fuzzer IN ITEMS fuzz_package_verifier fuzz_release_state fuzz_session_protocol)
        target_link_libraries(${fuzzer} PRIVATE lvgl_platform_contract)
        target_compile_options(${fuzzer} PRIVATE
            -Wall -Wextra -Wpedantic -Werror -fsanitize=fuzzer
        )
        target_link_options(${fuzzer} PRIVATE -fsanitize=fuzzer)
    endforeach()
endif()

if(BUILD_TESTING)
    add_executable(lvgl_sdk_public_header_tests
        tests/host/sdk_public_header_test.cpp
    )
    target_link_libraries(lvgl_sdk_public_header_tests PRIVATE lvgl_sdk_headers)
    if(MSVC)
        target_compile_options(lvgl_sdk_public_header_tests PRIVATE /W4 /WX)
    else()
        target_compile_options(lvgl_sdk_public_header_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror
        )
    endif()
    add_test(NAME lvgl_sdk_public_header_tests COMMAND lvgl_sdk_public_header_tests)

    add_executable(lvgl_platform_contract_tests
        tests/host/platform_contract_test.cpp
    )
    target_link_libraries(lvgl_platform_contract_tests PRIVATE
        lvgl_platform_contract
    )
    if(MSVC)
        target_compile_options(lvgl_platform_contract_tests PRIVATE /W4 /WX)
    else()
        target_compile_options(lvgl_platform_contract_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror
        )
    endif()
    target_compile_definitions(lvgl_platform_contract_tests PRIVATE
        LVGL_PLATFORM_TEST_PROFILE="${CMAKE_SOURCE_DIR}/profiles/y01-4.8.6.json"
        LVGL_PLATFORM_TEST_DEV_PACKAGE="${CMAKE_SOURCE_DIR}/tests/vectors/lvapp/development.lvapp.dev"
        LVGL_PLATFORM_TEST_SIGNED_PACKAGE="${CMAKE_SOURCE_DIR}/tests/vectors/lvapp/signed.lvapp"
    )
    add_test(NAME lvgl_platform_contract_tests
        COMMAND lvgl_platform_contract_tests
    )

    add_executable(game_2048_model_tests
        apps/game-2048/game_2048_model.cpp
        apps/game-2048/game_2048_persistence.cpp
        src/runtime/app_storage.cpp
        tests/host/game_2048_model_test.cpp
    )
    target_include_directories(game_2048_model_tests PRIVATE apps/game-2048 src)
    target_link_libraries(game_2048_model_tests PRIVATE
        lvgl_platform_contract
        lvgl_sdk_headers
    )
    if(MSVC)
        target_compile_options(game_2048_model_tests PRIVATE /W4 /WX)
    else()
        target_compile_options(game_2048_model_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror
        )
    endif()
    add_test(NAME game_2048_model_tests COMMAND game_2048_model_tests)

    add_executable(game_2048_visual_test
        apps/game-2048/game_2048_model.cpp
        apps/game-2048/game_2048_persistence.cpp
        apps/game-2048/game_2048_ui.cpp
        src/runtime/app_storage.cpp
        src/shell/app_theme.cpp
        src/assets/lv_font_app_16.c
        tests/host/game_2048_visual_test.cpp
    )
    target_include_directories(game_2048_visual_test PRIVATE src apps/game-2048)
    target_link_libraries(game_2048_visual_test PRIVATE
        lvgl::lvgl
        lvgl_platform_contract
        lvgl_sdk_headers
    )
    target_compile_definitions(game_2048_visual_test PRIVATE
        LVGL_PLATFORM_VISUAL_TESTING=1
    )
    if(MSVC)
        target_compile_options(game_2048_visual_test PRIVATE /W4 /WX /utf-8 /wd4996)
    else()
        target_compile_options(game_2048_visual_test PRIVATE
            -Wall -Wextra -Wpedantic -Werror
        )
    endif()
    add_test(NAME game_2048_visual_test
        COMMAND game_2048_visual_test "${CMAKE_SOURCE_DIR}/test-results")
endif()

add_library(device_profile STATIC
    src/platform/device_profile/device_profile.cpp
)
target_include_directories(device_profile PUBLIC src)
if(MSVC)
    target_compile_options(device_profile PRIVATE /W4 /WX)
else()
    target_compile_options(device_profile PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_library(app_registry STATIC
    src/session/app_registry.cpp
)
target_include_directories(app_registry PUBLIC src)
target_link_libraries(app_registry PUBLIC lvgl_platform_contract)
if(MSVC)
    target_compile_options(app_registry PRIVATE /W4 /WX)
else()
    target_compile_options(app_registry PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_library(session_protocol STATIC
    src/session/session_protocol.cpp
)
target_include_directories(session_protocol PUBLIC src)
target_link_libraries(session_protocol PUBLIC app_registry)
if(MSVC)
    target_compile_options(session_protocol PRIVATE /W4 /WX)
else()
    target_compile_options(session_protocol PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_library(app_control STATIC
    src/runtime/app_control.cpp
    src/runtime/app_storage.cpp
    src/runtime/installer_client.cpp
)
target_include_directories(app_control PUBLIC src)
target_link_libraries(app_control PUBLIC
    session_protocol
    lvgl_platform_contract
    lvgl_sdk_headers
)
if(MSVC)
    target_compile_options(app_control PRIVATE /W4 /WX)
else()
    target_compile_options(app_control PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_library(app_shell STATIC
    src/shell/app_theme.cpp
    src/shell/app_shell.cpp
    src/assets/lv_font_app_16.c
)
target_include_directories(app_shell PUBLIC src)
target_link_libraries(app_shell PUBLIC app_control lvgl::lvgl lvgl_sdk_headers)
if(MSVC)
    target_compile_options(app_shell PRIVATE /W4 /WX /utf-8 /wd4996)
else()
    target_compile_options(app_shell PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(NOT WIN32)
    add_executable(lvgl_sessiond
        platform/src/session/session_daemon.cpp
    )
    set_target_properties(lvgl_sessiond PROPERTIES OUTPUT_NAME "lvgl-sessiond")
    target_link_libraries(lvgl_sessiond PRIVATE lvgl_platform_contract)
    target_compile_options(lvgl_sessiond PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

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

    add_library(platform_runtime STATIC
        src/runtime/platform_runtime.cpp
        src/runtime/runtime_sandbox.cpp
        src/diagnostics/runtime_metrics.cpp
        src/platform/drm/drm_backend.cpp
        src/platform/input/input_backend.cpp
    )
    target_include_directories(platform_runtime PUBLIC
        src
        third_party/libdrm
        third_party/libdrm/include
        third_party/libdrm/include/drm
    )
    target_link_libraries(platform_runtime PUBLIC
        device_profile
        device_libdrm
        lvgl::lvgl
        app_control
        lvgl_platform_contract
        lvgl_sdk_headers
    )
    target_compile_options(platform_runtime PRIVATE
        -Wall -Wextra -Wpedantic -Werror -Wno-pedantic
    )

    add_executable(lvgl_poc
        src/lvgl_poc.cpp
        src/app/poc_ui.cpp
        src/assets/lv_font_poc_16.c
    )
    target_include_directories(lvgl_poc PRIVATE src)
    target_link_libraries(lvgl_poc PRIVATE
        platform_runtime
        app_shell
    )
    target_compile_options(lvgl_poc PRIVATE
        -Wall -Wextra -Wpedantic -Werror -Wno-pedantic
    )

    add_executable(lvgl_launcher
        apps/launcher/launcher_main.cpp
        apps/launcher/launcher_ui.cpp
    )
    set_target_properties(lvgl_launcher PROPERTIES OUTPUT_NAME "lvgl-desktop")
    target_include_directories(lvgl_launcher PRIVATE src apps)
    target_link_libraries(lvgl_launcher PRIVATE
        platform_runtime
        app_shell
        app_registry
    )
    target_compile_options(lvgl_launcher PRIVATE
        -Wall -Wextra -Wpedantic -Werror -Wno-pedantic
    )

    add_executable(focus_timer
        apps/focus_timer/focus_timer_main.cpp
        apps/focus_timer/focus_timer_model.cpp
        apps/focus_timer/focus_timer_ui.cpp
    )
    target_include_directories(focus_timer PRIVATE src apps)
    target_link_libraries(focus_timer PRIVATE
        platform_runtime
        app_shell
    )
    target_compile_options(focus_timer PRIVATE
        -Wall -Wextra -Wpedantic -Werror -Wno-pedantic
    )

    lvgl_add_application(game_2048
        APP_ID top.lvgl.game2048
        OUTPUT_NAME lvgl-2048
        SOURCES
            apps/game-2048/main.cpp
            apps/game-2048/game_2048_model.cpp
            apps/game-2048/game_2048_persistence.cpp
            apps/game-2048/game_2048_ui.cpp
        INCLUDE_DIRECTORIES apps/game-2048
    )

    if(LVGL_BUILD_SDK_TEMPLATE)
        add_subdirectory(sdk/template-app)
    endif()

    add_executable(lvgl_installer
        apps/installer/main.cpp
        apps/installer/installer_ui.cpp
    )
    set_target_properties(lvgl_installer PROPERTIES OUTPUT_NAME "lvgl-installer")
    target_include_directories(lvgl_installer PRIVATE src apps)
    target_link_libraries(lvgl_installer PRIVATE platform_runtime app_shell lvgl_platform_contract)
    target_compile_options(lvgl_installer PRIVATE
        -Wall -Wextra -Wpedantic -Werror -Wno-pedantic
    )

    install(TARGETS
        drm_probe
        drm_color_test
        lvgl_sessiond
        lvgl_launcher
        lvgl_poc
        focus_timer
        game_2048
        lvgl_installer
        RUNTIME DESTINATION bin
    )
endif()

install(DIRECTORY sdk/include/ DESTINATION include COMPONENT sdk)
install(FILES cmake/LvglApplication.cmake
    DESTINATION lib/cmake/lvgl-platform
    COMPONENT sdk
)

cmake_minimum_required(VERSION 3.10)

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

if(DEFINED ENV{CROSS_C_COMPILER} AND DEFINED ENV{CROSS_CXX_COMPILER})
  set(CMAKE_C_COMPILER "$ENV{CROSS_C_COMPILER}")
  set(CMAKE_CXX_COMPILER "$ENV{CROSS_CXX_COMPILER}")
elseif(DEFINED ENV{CROSS_TOOLCHAIN_PREFIX})
  set(CMAKE_C_COMPILER "$ENV{CROSS_TOOLCHAIN_PREFIX}gcc")
  set(CMAKE_CXX_COMPILER "$ENV{CROSS_TOOLCHAIN_PREFIX}g++")
else()
  message(FATAL_ERROR "Set compilers from the Y01 device profile")
endif()

project(jsapi_lvgl_launcher C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(SDK "${CMAKE_SOURCE_DIR}/iot-miniapp-sdk")

file(GLOB_RECURSE SDK_SOURCES "${SDK}/src/*.cpp")
add_library(iot_sdk STATIC ${SDK_SOURCES})
target_compile_options(iot_sdk PRIVATE -w)
target_include_directories(iot_sdk PUBLIC "${SDK}/include")
set_target_properties(iot_sdk PROPERTIES POSITION_INDEPENDENT_CODE ON)

file(GLOB_RECURSE PLUGIN_SOURCES "${CMAKE_SOURCE_DIR}/src/*.cpp")
add_library(jsapi_lvgl_launcher SHARED ${PLUGIN_SOURCES})
target_include_directories(jsapi_lvgl_launcher PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_options(jsapi_lvgl_launcher PRIVATE
  -O3 -Wall -Wextra -Werror=return-type -Wno-psabi -fvisibility=hidden)
target_link_libraries(jsapi_lvgl_launcher PRIVATE
  iot_sdk pthread -Wl,--unresolved-symbols=ignore-all)
set_target_properties(jsapi_lvgl_launcher PROPERTIES
  OUTPUT_NAME "jsapi_lvgl_launcher"
  PREFIX "lib")

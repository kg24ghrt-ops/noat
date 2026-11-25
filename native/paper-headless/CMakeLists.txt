cmake_minimum_required(VERSION 3.21)
project(paper-headless CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED NOAT_VULKAN_SDK)
    set(NOAT_VULKAN_SDK "" CACHE STRING "Path to Vulkan SDK (host build only)")
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2")

# Approved dependencies (vendored into third_party/):
# VMA (memory), stb_image_write, stb_image (PNG). Raw Vulkan calls otherwise.
set(THIRD_PARTY "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

add_executable(paper-headless
    main.cpp
)

target_include_directories(paper-headless PRIVATE
    ${THIRD_PARTY}
)

if(ANDROID)
    # NDK provides vulkan headers + the device provides libvulkan.so at runtime.
    find_library(VULKAN_LIBRARY NAMES vulkan REQUIRED)
    target_link_libraries(paper-headless PRIVATE
        ${VULKAN_LIBRARY}
        -static-libstdc++
    )
    set_target_properties(paper-headless PROPERTIES
        LINK_FLAGS "-s"
    )
else()
    # Host build: link system Vulkan loader for lavapipe validation.
    find_package(Vulkan REQUIRED)
    target_link_libraries(paper-headless PRIVATE
        Vulkan::Vulkan
    )
endif()

target_compile_definitions(paper-headless PRIVATE
    NOAT_HEADLESS=1
)

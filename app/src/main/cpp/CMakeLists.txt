cmake_minimum_required(VERSION 3.21)
project(paper-engine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2 -DNDEBUG")

# Approved dependencies: VMA (memory), stb_image_write (PNG).
# Raw Vulkan calls otherwise. No Magnum, no Corrade, no flextVk.
set(THIRD_PARTY "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

# NDK provides vulkan headers + the device provides libvulkan.so at runtime.
find_library(VULKAN_LIBRARY NAMES vulkan REQUIRED)
find_library(EGL_LIBRARY NAMES EGL REQUIRED)

add_library(paper-engine SHARED
    renderer.cpp
    noat_jni.cpp
    paper_wear_spv.h
)

target_include_directories(paper-engine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${THIRD_PARTY}
)

target_link_libraries(paper-engine PRIVATE
    ${VULKAN_LIBRARY}
    android
    ${EGL_LIBRARY}
    -static-libstdc++
)

set_target_properties(paper-engine PROPERTIES
    LINK_FLAGS "-s"
)

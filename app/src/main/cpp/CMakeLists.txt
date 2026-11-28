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
    text_shaper.cpp
    shape_run.cpp
    sdf_atlas.cpp
    text_document.cpp
    paper_wear_spv.h
)

target_include_directories(paper-engine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${THIRD_PARTY}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../native/paper-headless
)

target_link_libraries(paper-engine PRIVATE
    ${VULKAN_LIBRARY}
    android
    ${EGL_LIBRARY}
    -static-libstdc++
)

# HarfBuzz (complex-script shaping) - wired but inactive until a font asset and
# an NDK HarfBuzz build step are approved. Guards keep the APK lane unchanged.
if(DEFINED HARFBUZZ_LIBRARY AND HARFBUZZ_LIBRARY)
    target_link_libraries(paper-engine PRIVATE ${HARFBUZZ_LIBRARY})
    if(DEFINED HARFBUZZ_INCLUDE_DIR AND HARFBUZZ_INCLUDE_DIR)
        target_include_directories(paper-engine PRIVATE ${HARFBUZZ_INCLUDE_DIR})
    endif()
    target_compile_definitions(paper-engine PRIVATE NOAT_HAVE_HARFBUZZ=1)
endif()

set_target_properties(paper-engine PROPERTIES
    LINK_FLAGS "-s"
)

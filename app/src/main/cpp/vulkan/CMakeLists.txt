# Vulkan paper rendering engine
# Targets ARM Mali GPUs with tile-based rendering optimizations via subpasses

set(VULKAN_ENGINE_SRC
    context.cpp
    renderpass.cpp
    pipeline.cpp
    resources.cpp
    frame.cpp
    renderer.cpp
    material/material.cpp
    jni/vulkan_jni.cpp
)

set(VULKAN_ENGINE_HDR
    context.h
    renderpass.h
    pipeline.h
    resources.h
    frame.h
    renderer.h
    material/material.h
)

add_library(
    paper-engine
    SHARED
    ${VULKAN_ENGINE_SRC}
    ${VULKAN_ENGINE_HDR}
)

target_include_directories(
    paper-engine
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(
    paper-engine
    PRIVATE cxx_std_17
)

find_library(VULKAN_LIB vulkan REQUIRED)
find_library(ANDROID_NATIVE_WINDOW_LIB android REQUIRED)
find_library(log-lib log)

target_link_libraries(
    paper-engine
    PRIVATE
    ${VULKAN_LIB}
    ${ANDROID_NATIVE_WINDOW_LIB}
    ${log-lib}
)

# Optimization flags for Mali Valhall architecture
target_compile_options(
    paper-engine
    PRIVATE
    -O2
    -Wall
    -Wextra
    -fvisibility=hidden
    -ffast-math
    -fno-rtti
)

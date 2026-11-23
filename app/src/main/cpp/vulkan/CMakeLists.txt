# Vulkan paper rendering engine
# Targets ARM Mali GPUs with tile-based rendering optimizations via subpasses

set(VULKAN_ENGINE_SRC
    noat_context.c
    noat_renderpass.c
    noat_pipeline.c
    noat_resources.c
    noat_frame.c
    noat_renderer.c
    noat_paper_geometry.c
    material/noat_material.c
    jni/noat_jni.c
)

set(VULKAN_ENGINE_HDR
    noat_vulkan.h
    noat_context.h
    noat_renderpass.h
    noat_pipeline.h
    noat_resources.h
    noat_frame.h
    noat_renderer.h
    noat_paper_geometry.h
    material/noat_material.h
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

# Compile as C11 standard
set_target_properties(paper-engine PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
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
)

# C-specific flags
target_compile_options(paper-engine PRIVATE
    -std=c11
    -Wall
    -Wextra
    -Wpedantic
    -Wno-unused-parameter
    -fvisibility=hidden
)

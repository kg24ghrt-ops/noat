# Paper rendering engine - C++17, OpenGL ES 3.0-3.2
# Targets ARM Mali GPUs with tile-based rendering optimizations

set(PAPER_ENGINE_SRC
    core/capabilities.cpp
    rendering/shader.cpp
    rendering/program.cpp
    rendering/texture.cpp
    rendering/geometry.cpp
    rendering/renderer.cpp
    material/material.cpp
    jni/paper_jni.cpp
)

set(PAPER_ENGINE_HDR
    core/capabilities.h
    core/gl_context.h
    core/gl_object.hpp
    rendering/shader.h
    rendering/program.h
    rendering/texture.h
    rendering/geometry.h
    rendering/renderer.h
    material/material.h
)

add_library(
    paper-engine
    SHARED
    ${PAPER_ENGINE_SRC}
    ${PAPER_ENGINE_HDR}
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

find_library(GLESv3_LIB GLESv3 REQUIRED)
find_library(EGL_LIB EGL REQUIRED)
find_library(android_LIB android REQUIRED)
find_library(log-lib log)

target_link_libraries(
    paper-engine
    PRIVATE
    ${GLESv3_LIB}
    ${EGL_LIB}
    ${android_LIB}
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

target_compile_definitions(
    paper-engine
    PRIVATE
    PAPER_ENGINE_EXPORT=__attribute__((visibility("default")))
)

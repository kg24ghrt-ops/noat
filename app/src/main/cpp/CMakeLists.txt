cmake_minimum_required(VERSION 3.31.6)

project("noat" LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(
    native-lib
    SHARED
    native-lib.cpp
)

target_compile_features(native-lib PRIVATE cxx_std_17)

find_library(
    log-lib
    log
)

target_link_libraries(
    native-lib
    ${log-lib}
)

add_subdirectory(paper)

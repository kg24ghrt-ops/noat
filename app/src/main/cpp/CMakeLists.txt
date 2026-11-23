cmake_minimum_required(VERSION 3.31.6)

project("noat" LANGUAGES C)

find_library(
    log-lib
    log
)

add_subdirectory(vulkan)

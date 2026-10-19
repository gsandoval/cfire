cmake_minimum_required(VERSION 3.20)
project(libkv_cmake C)

add_library(kvstore STATIC src/kvstore.c)
target_include_directories(kvstore PUBLIC include)
target_compile_definitions(kvstore PRIVATE KV_SALT=42)

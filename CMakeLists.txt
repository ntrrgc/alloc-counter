cmake_minimum_required(VERSION 2.8)

project(alloc-counter)

add_library(alloc-counter SHARED
    "xxhash.h"
    "xxhash.c"
    "comm-memory.h"
    "comm-memory.cpp"
    "library-context.h"
    "library-context.cpp"
    "wrapper-malloc.cpp"
    "patrol-thread.h"
    "patrol-thread.cpp"
    "init.cpp"
    "allocation-table.h"
    "allocation-table.cpp"
    "allocation-stats.h"
    "allocation-stats.cpp"
    "callstack-fingerprint.h"
    "callstack-fingerprint.cpp"
    )

target_compile_options(alloc-counter PUBLIC -Wall -std=c++14)
target_link_libraries(alloc-counter dl pthread)
target_compile_definitions(alloc-counter PUBLIC _GNU_SOURCE)

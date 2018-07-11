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
    "environment.h"
    "environment.cpp"
    "memory-protector.h"
    "memory-protector.cpp"
    "stack-trace.h"
    "stack-trace.cpp"
    "watched-stack-trace-info.h"
    "watched-stack-trace-info.cpp"
    )

target_compile_options(alloc-counter PUBLIC -Wall -std=c++14)
target_link_libraries(alloc-counter dl pthread unwind)
target_compile_definitions(alloc-counter PUBLIC _GNU_SOURCE)

add_executable(alloc-counter-start
    "alloc-counter-start.cpp")

add_library(dummy-lib SHARED
    "dummy-lib.h"
    "dummy-lib.cpp"
    )
target_compile_options(dummy-lib PUBLIC -Wall -std=c++14)

add_executable(test-stack-traces
    "stack-trace.h"
    "stack-trace.cpp"
    "test-stack-traces.cpp"
    )
target_compile_options(test-stack-traces PUBLIC -Wall -std=c++14)
target_link_libraries(test-stack-traces dl pthread unwind dummy-lib)
target_compile_definitions(test-stack-traces PUBLIC _GNU_SOURCE)

add_executable(leaking-app
    "leaking-app.cpp"
    )
target_compile_options(leaking-app PUBLIC -Wall -std=c++14)
target_link_libraries(leaking-app dummy-lib)

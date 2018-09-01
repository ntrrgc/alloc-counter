cmake_minimum_required(VERSION 2.8)
project(alloc-counter)

option(WITH_TESTS "Build tests" ON)

add_library(alloc-counter SHARED
    "common/environment.h"
    "common/environment.cpp"
    "common/stack-trace.h"
    "common/stack-trace.cpp"
    "common/library-context.h"
    "common/library-context.cpp"
    "alloc-counter/comm-memory.h"
    "alloc-counter/comm-memory.cpp"
    "alloc-counter/wrapper-malloc.cpp"
    "alloc-counter/patrol-thread.h"
    "alloc-counter/patrol-thread.cpp"
    "alloc-counter/init.cpp"
    "alloc-counter/allocation-table.h"
    "alloc-counter/allocation-table.cpp"
    "alloc-counter/allocation-stats.h"
    "alloc-counter/allocation-stats.cpp"
    "alloc-counter/callstack-fingerprint.h"
    "alloc-counter/callstack-fingerprint.cpp"
    "alloc-counter/memory-protector.h"
    "alloc-counter/memory-protector.cpp"
    "alloc-counter/watched-stack-trace-info.h"
    "alloc-counter/watched-stack-trace-info.cpp"
    )
target_include_directories(alloc-counter BEFORE PRIVATE common alloc-counter)
target_compile_options(alloc-counter PUBLIC -Wall -std=c++14)
target_link_libraries(alloc-counter dl pthread unwind)
target_compile_definitions(alloc-counter PUBLIC _GNU_SOURCE)

add_library(mmap-counter SHARED
    "common/stack-trace.h"
    "common/stack-trace.cpp"
    "common/environment.h"
    "common/environment.cpp"
    "common/library-context.h"
    "common/library-context.cpp"
    "mmap-counter/interned-stack-trace.h"
    "mmap-counter/interned-stack-trace.cpp"
    "mmap-counter/memory-map.h"
    "mmap-counter/memory-map.cpp"
    "mmap-counter/wrapper-mmap.cpp"
    )
target_include_directories(mmap-counter BEFORE PRIVATE common mmap-counter)
target_compile_options(mmap-counter PUBLIC -Wall -std=c++14)
target_link_libraries(mmap-counter dl pthread unwind)
target_compile_definitions(mmap-counter PUBLIC _GNU_SOURCE)
set_target_properties(mmap-counter PROPERTIES CXX_VISIBILITY_PRESET hidden)

if(WITH_TESTS)
    add_executable(mmap-counter-tests
        "common/stack-trace.cpp"
        "common/environment.cpp"
        "mmap-counter/interned-stack-trace.cpp"
        "mmap-counter-tests/test-memory-map.cpp")
    target_include_directories(mmap-counter-tests BEFORE PRIVATE vendor common mmap-counter)
    target_compile_options(mmap-counter-tests PUBLIC -Wall -std=c++14)
    target_link_libraries(mmap-counter-tests dl pthread unwind gtest)
    target_compile_definitions(mmap-counter-tests PUBLIC _GNU_SOURCE)
endif()

add_executable(test-mmap
    "test-mmap/test-mmap.cpp")

add_executable(alloc-counter-start
    "alloc-counter-start/alloc-counter-start.cpp")

add_library(mallinfo-log SHARED
    "mallinfo-log/libmallinfo-log.cpp"
    )
target_compile_options(mallinfo-log PUBLIC -Wall -std=c++14)
target_link_libraries(mallinfo-log pthread)
target_compile_definitions(mallinfo-log PUBLIC _GNU_SOURCE)

add_library(dummy-lib SHARED
    "dummy-lib/dummy-lib.h"
    "dummy-lib/dummy-lib.cpp"
    )
target_include_directories(dummy-lib BEFORE PRIVATE dummy-lib)
target_compile_options(dummy-lib PUBLIC -Wall -std=c++14)

add_executable(test-stack-traces
    "common/stack-trace.h"
    "common/stack-trace.cpp"
    "common/environment.h"
    "common/environment.cpp"
    "test-stack-traces/test-stack-traces.cpp"
    )
target_include_directories(test-stack-traces BEFORE PRIVATE common test-stack-traces dummy-lib)
target_compile_options(test-stack-traces PUBLIC -Wall -std=c++14)
target_link_libraries(test-stack-traces dl pthread unwind dummy-lib)
target_compile_definitions(test-stack-traces PUBLIC _GNU_SOURCE)

add_executable(leaking-app
    "leaking-app/leaking-app.cpp"
    )
target_include_directories(leaking-app BEFORE PRIVATE dummy-lib)
target_compile_options(leaking-app PUBLIC -Wall -std=c++14)
target_link_libraries(leaking-app dummy-lib)

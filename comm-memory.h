#pragma once
#include <cstdint>

enum class WatchState : int32_t {
    NotWatching = 0,
    Watching = 1
};

extern WatchState * __commMemory;

WatchState inline __attribute((always_inline)) getWatchState() {
    return *__commMemory;
}

void initCommMemory();

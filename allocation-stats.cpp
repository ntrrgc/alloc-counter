#include "allocation-stats.h"
#include <time.h>
#include <cassert>

AllocationStats AllocationStats::s_instance;

double AllocationStats::getTime() {
    timespec tv;
    clock_gettime(CLOCK_MONOTONIC, &tv);
    return tv.tv_sec + tv.tv_nsec / 1e9;
}

void AllocationStats::enable() {
    lock_guard<mutex> lock(m_mutex);
    if (enabled)
        return; // another thread already did before us
    enabled = true;
    timeWatchEnabled = getTime();
    allocationCount.store(0);
    freeCount.store(0);
    reallocCount.store(0);
}

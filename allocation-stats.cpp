#include "allocation-stats.h"
#include <time.h>
#include <cassert>

double AllocationStats::getTime() {
    timespec tv;
    clock_gettime(CLOCK_MONOTONIC, &tv);
    return tv.tv_sec + tv.tv_nsec / 1e9;
}

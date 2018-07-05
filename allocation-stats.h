#pragma once
#include <atomic>
#include <mutex>
using namespace std;

class AllocationStats {
public:
    unsigned long allocationCount = 0;
    unsigned long freeCount = 0;
    unsigned long reallocCount = 0;

    double timeWatchEnabled = -1;
    bool enabled = false;

    static double getTime();

    void inline ensureEnabled() {
        if (!enabled) {
            enabled = true;
            timeWatchEnabled = getTime();
        }
    }
};

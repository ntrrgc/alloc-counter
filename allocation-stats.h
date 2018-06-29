#pragma once
#include <atomic>

class AllocationStats {
public:
    std::atomic<unsigned long> allocationCount;
    std::atomic<unsigned long> freeCount;
    std::atomic<unsigned long> reallocCount;

    double timeWatchEnabled = -1;
    bool enabled = false;

    static inline AllocationStats& instance() {
        return s_instance;
    }

    static double getTime();

    void inline ensureEnabled() {
        if (!enabled)
            enable();
    }

private:
    void enable();

    static AllocationStats s_instance;
};

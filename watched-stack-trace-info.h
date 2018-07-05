#pragma once
#include "stack-trace.h"
#include "environment.h"
#include <atomic>
#include <mutex>
using namespace std;

enum class Trilean {
    Unknown = 0,
    True = 1,
    False = -1
};

struct WatchedStackTraceInfo {
    explicit WatchedStackTraceInfo(StackTrace stackTrace)
        : stackTrace(stackTrace)
    {}
    StackTrace stackTrace;

    // Statistics for this stack trace:
    uint32_t countTotalCloselyWatchedAllocationsEverCreated = 0;
    uint32_t countLiveCloselyWatchedAllocations = 0;
    uint32_t countLeakedCloselyWatchedAllocations = 0;
    size_t   countTotalLeakedMemory = 0;
    uint64_t countSkippedAllocations = 0;

    // Hold this mutex briefly while reading or updating any of the above stats.
    mutable std::mutex statsMutex; // TODO should the AllocationTable lock suffice?

    // Global statistics:
    // There is a limit on the number of closely watched allocations because
    // the number of sections we can mprotect() is limited (65k in Linux x86_64).
    static atomic<uint32_t> countLiveCloselyWatchedAllocationsAllTraces;

    // 1.0 -> leaks always, 0.0 -> never leaks, NaN -> no info
    float leakRate() const {
        lock_guard<mutex> lock(statsMutex);
        return (float) countLeakedCloselyWatchedAllocations / countFinishedWatchedAllocationsUnlocked();
    }

    Trilean hasLeaks() const {
        lock_guard<mutex> lock(statsMutex);
        return hasLeaksUnlocked();
    }

    bool needsMoreCloselyWatchedAllocations() const {
        lock_guard<mutex> lock(statsMutex);
        if (countLiveCloselyWatchedAllocations >= environment.maxLiveCloselyWatchedAllocationsPerTrace)
            return false;
        if (countLiveCloselyWatchedAllocationsAllTraces >= environment.globalMaxLiveCloselyWatchedAllocations)
            return false;
        // If it has proven not to be a leak, it's fine, we don't need more samples...
        // BUT if it has proven to be a leak we still want more samples to know how often that happens.
        return hasLeaksUnlocked() != Trilean::False;
    }

private:
    uint32_t countFinishedWatchedAllocationsUnlocked() const {
        return countTotalCloselyWatchedAllocationsEverCreated -
            countLiveCloselyWatchedAllocations;
    }

    Trilean hasLeaksUnlocked() const {
        if (countLeakedCloselyWatchedAllocations > 0)
            return Trilean::True;
        else if (countFinishedWatchedAllocationsUnlocked() >= environment.enoughSamplesToProveNoLeak)
            return Trilean::False;
        else
            return Trilean::Unknown;
    }
};

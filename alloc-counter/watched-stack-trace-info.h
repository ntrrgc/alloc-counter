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

    // Global statistics:
    // There is a limit on the number of closely watched allocations because
    // the number of sections we can mprotect() is limited (65k in Linux x86_64).
    static uint32_t countLiveCloselyWatchedAllocationsAllTraces;

    // 1.0 -> leaks always, 0.0 -> never leaks, NaN -> no info
    float leakRatio() const {
        return (float) countLeakedCloselyWatchedAllocations / countFinishedWatchedAllocations();
    }

    Trilean hasLeaks() const {
        if (countLeakedCloselyWatchedAllocations > 0)
            return Trilean::True;
        else if (countFinishedWatchedAllocations() >= environment.enoughSamplesToProveNoLeak)
            return Trilean::False;
        else
            return Trilean::Unknown;
    }

    bool needsMoreCloselyWatchedAllocations() const {
        if (countLiveCloselyWatchedAllocations >= environment.maxLiveCloselyWatchedAllocationsPerTrace)
            return false;
        if (countLiveCloselyWatchedAllocationsAllTraces >= environment.globalMaxLiveCloselyWatchedAllocations)
            return false;
        // If it has proven not to be a leak, it's fine, we don't need more samples...
        // BUT if it has proven to be a leak we still want more samples to know how often that happens.
        return hasLeaks() != Trilean::False;
    }

    float lostAllocationsEstimated() const {
        if (countFinishedWatchedAllocations() == 0)
            return 0;
        return countTotalAllocations() * leakRatio();
    }

    float lostBytesEstimated() const {
        return countTotalLeakedMemory / watchRate();
    }

private:
    float watchRate() const {
        return (float) countTotalCloselyWatchedAllocationsEverCreated / countTotalAllocations();
    }

    uint32_t countTotalAllocations() const {
        return countSkippedAllocations + countTotalCloselyWatchedAllocationsEverCreated;
    }

    uint32_t countFinishedWatchedAllocations() const {
        return countTotalCloselyWatchedAllocationsEverCreated -
            countLiveCloselyWatchedAllocations;
    }
};

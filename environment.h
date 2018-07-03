#pragma once
#include <cstdint>

struct Environment {
    /** Maximum expected life of most (non-leaky) allocations.
     *
     * For light allocations (initially, all allocations), when this time is
     * surpassed, successive allocations coming from that code*
     * will be closely watched to determine if they are leaks or just
     * long lived allocations.
     *
     * When a closely watched allocations reaches this time without being
     * freed, it becomes suspicious and a mprotect()-based watchpoint is
     * enabled.
     *
     * * Note: since light allocations have very rough stack fingerprints,
     *   new allocations coming from unrelated, innocent code may also become
     *   closely watched accidentally. */
    uint32_t timeForAllocationToBecomeSuspicious = parseEnvironIntGreaterThanZero("ALLOC_TIME_SUSPICIOUS", 30);

    /**
     * @brief closelyWatchedAllocationsRestTime
     * After the watchpoint of a formerly suspicious closely watched allocation
     * has been hit, the allocation will not get further watchpoints for this
     * interval of time.
     *
     * After this interval of time, it will be suspicious again.
     */
    uint32_t closelyWatchedAllocationsRestTime = parseEnvironIntGreaterThanZero("ALLOC_REST_TIME", 10);

    /** Once this many closely watched allocations have expired without being
     * found to be leaks, the associated stack trace will be consider innocent
     * and no more allocations coming from it will be closely watched. */
    uint32_t enoughSamplesToProveNoLeak = parseEnvironIntGreaterThanZero("ALLOC_NO_SAMPLES_NO_LEAK", 5);

    uint32_t globalMaxLiveCloselyWatchedAllocations = parseEnvironIntGreaterThanZero("ALLOC_GLOBAL_MAX_CLOSELY_WATCHED", 50000);
    uint32_t maxLiveCloselyWatchedAllocationsPerTrace = parseEnvironIntGreaterThanZero("ALLOC_MAX_CLOSELY_WATCHED", 5);


private:
    static unsigned int parseEnvironIntGreaterThanZero(const char* name, int defaultValue);
};

extern Environment environment;

#pragma once
#include <cstdint>
#include <unistd.h>
#include <sys/utsname.h>
#include <string>
using namespace std;

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

    /** Once a closely watched allocation enters suspicious state it has this
     * many second to receive an access and become non suspicious again.
     * Otherwise, it will be declared a leak. */
    // TODO This iss et to 1 because the memory protector is not integrated yet, so we'd rather consider it a leak at
    // this point rather than wait for an event that won't happen because it's not coded.
    uint32_t closelyWatchedAllocationsAccessMaxInterval = parseEnvironIntGreaterThanZero("ALLOC_MAX_ACCESS_INTERVAL", 1);

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
    uint32_t enoughSamplesToProveNoLeak = parseEnvironIntGreaterThanZero("ALLOC_ENOUGH_SAMPLES_TO_PROVE_NO_LEAK", 5);

    uint32_t globalMaxLiveCloselyWatchedAllocations = parseEnvironIntGreaterThanZero("ALLOC_GLOBAL_MAX_CLOSELY_WATCHED", 50000);
    uint32_t maxLiveCloselyWatchedAllocationsPerTrace = parseEnvironIntGreaterThanZero("ALLOC_MAX_CLOSELY_WATCHED", 30);

    uint32_t leakReportInterval = parseEnvironIntGreaterThanZero("ALLOC_LEAK_REPORT_INTERVAL", 30);

    uint32_t pageSize = sysconf(_SC_PAGESIZE);

    /** Once this much time passes (in seconds) memory checks will begin. At this point all application initialization
     * should have completed (to avoid reporting false leaks on startup artifacts.
     * Zero means auto start is disabled, memory checks will start when `alloc-counter-start` is invoked. */
    uint32_t autoStartTime = parseEnvironIntGreaterThanZero("ALLOC_AUTO_START_TIME", 0);

    std::string archName = []() noexcept {
        struct utsname utsname;
        int ret = uname(&utsname);
        if (ret != 0) {
            perror("uname");
            exit(1);
        }
        return std::string(utsname.machine);
    }();

    uint32_t roundUpToPageMultiple(uint32_t size) const {
        return (size + (pageSize - 1)) & ~(pageSize - 1);
    }

private:
    static unsigned int parseEnvironIntGreaterThanZero(const char* name, int defaultValue);
};

extern Environment environment;

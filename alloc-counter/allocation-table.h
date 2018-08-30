#include <cstdint>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <set>
#include <algorithm>
#include <functional>
#include <time.h>
#include <cassert>
#include <malloc.h>
#include <strings.h>
#include <string.h>
#include "callstack-fingerprint.h"
#include "environment.h"
#include "stack-trace.h"
#include "watched-stack-trace-info.h"
#include "allocation-stats.h"
#include "library-context.h"
#include "comm-memory.h"
using namespace std;

struct Allocation {
    void* memory;
    uint32_t requestedSize;
    // This is the time when, if reached, the allocation moves to a more suspicious state:
    // If it's a LightAllocation, the fast fingerprint will be marked as suspicious.
    // If it's a CloselyWatchedAllocation, it depends on its state:
    // -> If it is in NotYetSuspicious state, it will become Suspicious.
    // -> If it is in Suspicious state, it will be declared a leak.
    uint32_t deadline;

protected:
    Allocation() {}
};

struct LightAllocation : public Allocation {
    CallstackFingerprint fingerprint;
};

struct CloselyWatchedAllocation : public Allocation {
    enum class State {
        NotYetSuspicious = 0,
        Suspicious = 1
    };

    State state = State::NotYetSuspicious;
    uint32_t allocationTime;
    WatchedStackTraceInfo* watchedStackTraceInfo;

    uint32_t actualSize() const {
        return environment.roundUpToPageMultiple(this->requestedSize);
    }
};

class SuspiciousStackTracesTable : public unordered_map<StackTrace, WatchedStackTraceInfo> {
public:
    WatchedStackTraceInfo& getOrCreate(const StackTrace& trace) {
        auto emplaceRet = this->emplace(std::piecewise_construct, std::forward_as_tuple(trace), std::forward_as_tuple(trace));
        return emplaceRet.first->second;
    }
};

class SuspiciousFingerprintTable : public unordered_map<CallstackFingerprint, SuspiciousStackTracesTable> {
public:
    void addSuspiciousFingerprint(CallstackFingerprint fingerprint) {
        // no-operation if the fingerprint already exists
        this->emplace(std::piecewise_construct, make_tuple(fingerprint), make_tuple());
    }

    SuspiciousStackTracesTable* getSuspiciousStackTracesTable(CallstackFingerprint fingerprint) {
        iterator it = this->find(fingerprint);
        if (it == end())
            return nullptr;
        return &it->second;
    }
};

class AllocationTable {
public:
    AllocationTable() {}
    static AllocationTable& instance() { return s_allocationTable; }

    enum class ZeroFill {
        Unnecessary,
        Needed
    };

    static const uint32_t NoAlignment = 1;

    /** The malloc wrapper must call this *instead* of allocating the memory itself, as an special allocator may be
     * required for closely watched allocations. */
    void* instrumentedAllocate(uint32_t size, uint32_t alignment, CallstackFingerprint fingerprint, function<void*()> preferredAllocator, ZeroFill zeroFill) {
        if (LibraryContext::inLibrary() || getWatchState() == WatchState::NotWatching)
            return preferredAllocator();

        LibraryContext ctx;

        lock_guard<mutex> lock(m_mutex);
        m_stats.ensureEnabled();
        ++m_stats.allocationCount;
        SuspiciousStackTracesTable* stackTraceTable = m_suspiciousFingerprints.getSuspiciousStackTracesTable(fingerprint);
        if (!stackTraceTable) {
            // Unsuspicious fingerprint
            void* memory = preferredAllocator();
            if (!memory) {
                return nullptr;
            }
            LightAllocation& alloc = m_lightAllocationsByAddress[memory];
            alloc.fingerprint = fingerprint;
            alloc.memory = memory;
            alloc.requestedSize = size;
            alloc.deadline = time(nullptr) + environment.timeForAllocationToBecomeSuspicious;
            return memory;
        }

        ++m_stats.allocationWithSuspiciousFingerprintCount;
        StackTrace stackTrace;
        WatchedStackTraceInfo& watchedStackTraceInfo = stackTraceTable->getOrCreate(stackTrace);
        if (!watchedStackTraceInfo.needsMoreCloselyWatchedAllocations()) {
            // Suspicious stack, but we don't need to watch it (e.g. we have enough instances of that stack already).
            // No tracking is done at all in this case (there is no use on even using a LightAllocation... as the
            // purpose of a LightAllocation is becoming a CloselyWatchedAllocation if unfreed, and this has already
            // happened.
            watchedStackTraceInfo.countSkippedAllocations++;
            return preferredAllocator();
        }

        // Allocation coming from a suspicious stack we should watch.
        // memalign() will round `alignment` to the next power of two if necessary (unlikely) -- at least in glibc.
        void* memory = memalign(std::max(alignment, environment.pageSize), environment.roundUpToPageMultiple(size));
        if (!memory)
            return nullptr;

        if (zeroFill == ZeroFill::Needed)
            bzero(memory, size);

        watchedStackTraceInfo.countLiveCloselyWatchedAllocations++;
        watchedStackTraceInfo.countLiveCloselyWatchedAllocationsAllTraces++;
        watchedStackTraceInfo.countTotalCloselyWatchedAllocationsEverCreated++;

        CloselyWatchedAllocation& alloc = m_closelyWatchedAllocationsByAddress[memory];
        alloc.memory = memory;
        alloc.requestedSize = size; // less or equal the size actually allocated
        alloc.allocationTime = time(nullptr);
        alloc.deadline = alloc.allocationTime + environment.timeForAllocationToBecomeSuspicious;
        alloc.state = CloselyWatchedAllocation::State::NotYetSuspicious;
        alloc.watchedStackTraceInfo = &watchedStackTraceInfo;
        return memory;
    }

    void* instrumentedReallocate(void* oldMemory, size_t newRequestedSize, function<void*()> preferredReallocator) {
        if (LibraryContext::inLibrary() || getWatchState() == WatchState::NotWatching)
            return preferredReallocator();

        LibraryContext ctx;

        lock_guard<mutex> lock(m_mutex);
        m_stats.ensureEnabled();
        ++m_stats.reallocCount;

        {
            auto it = m_lightAllocationsByAddress.find(oldMemory);
            if (it != m_lightAllocationsByAddress.end()) {
                // Realloc LightAllocation
                LightAllocation& alloc = it->second;
                alloc.requestedSize = newRequestedSize;
                void* newMemory = preferredReallocator();
                if (newMemory != oldMemory) {
                    alloc.memory = newMemory;
                    m_lightAllocationsByAddress.insert(make_pair(newMemory, alloc));
                    m_lightAllocationsByAddress.erase(it);
                }
                return newMemory;
            }
        }

        {
            auto it = m_closelyWatchedAllocationsByAddress.find(oldMemory);
            if (it != m_closelyWatchedAllocationsByAddress.end()) {
                // Realloc CloselyWatchedAllocation
                CloselyWatchedAllocation& alloc = it->second;
                size_t oldActualSize = environment.roundUpToPageMultiple(alloc.requestedSize);
                size_t newActualSize = environment.roundUpToPageMultiple(newRequestedSize);
                if (newActualSize != oldActualSize) {
                    // TODO Remove watchpoint
                    // Unfortunately, there is no function to realloc aligned memory and keep the alignment, so we have
                    // to make a new allocation and copy memory.
                    void* newMemory = memalign(environment.pageSize, newActualSize);
                    memcpy(newMemory, oldMemory, alloc.requestedSize);
                    alloc.requestedSize = newRequestedSize;
                    alloc.memory = newMemory;
                    m_closelyWatchedAllocationsByAddress.insert(make_pair(newMemory, alloc));
                    m_closelyWatchedAllocationsByAddress.erase(it);
                    return newMemory;
                } else {
                    // The underlying size in pages is the same, so skip allocation.
                    alloc.requestedSize = newRequestedSize;
                    return oldMemory;
                }
            }
        }

        // Realloc uninstrumented allocation
        return preferredReallocator();
    }

    void instrumentedFree(void* memory, std::function<void()> freeFunction) {
        if (!memory) {
            // Nothing to do with free(NULL);
            return;
        }

        if (LibraryContext::inLibrary() || getWatchState() == WatchState::NotWatching) {
            freeFunction();
            return;
        }

        LibraryContext ctx;

        lock_guard<mutex> lock(m_mutex);
        m_stats.ensureEnabled();
        ++m_stats.freeCount;

        {
            auto it = m_lightAllocationsByAddress.find(memory);
            if (it != m_lightAllocationsByAddress.end()) {
                m_lightAllocationsByAddress.erase(it);
                goto freeAndReturn;
            }
        }

        {
            auto it = m_closelyWatchedAllocationsByAddress.find(memory);
            if (it != m_closelyWatchedAllocationsByAddress.end()) {
                CloselyWatchedAllocation& alloc = it->second;
                alloc.watchedStackTraceInfo->countLiveCloselyWatchedAllocations--;
                alloc.watchedStackTraceInfo->countLiveCloselyWatchedAllocationsAllTraces--;
                // TODO Remove watchpoint
                m_closelyWatchedAllocationsByAddress.erase(it);
            }
        }

freeAndReturn:
        freeFunction();
    }

    struct FoundLeak {
        StackTrace* stackTrace;
        void* memory;
        uint32_t size;
    };

    // To be called from Patrol Thread only
    std::tuple<AllocationStats, vector<FoundLeak>> patrolThreadUpdateAllocationStates() {
        uint32_t now = time(nullptr);
        lock_guard<mutex> lock(m_mutex);
        vector<FoundLeak> foundLeaks;

        for (auto it = m_lightAllocationsByAddress.begin(); it != m_lightAllocationsByAddress.end(); ) {
            LightAllocation& alloc = it->second;
            if (alloc.deadline < now) {
                m_suspiciousFingerprints.addSuspiciousFingerprint(alloc.fingerprint);
                it = m_lightAllocationsByAddress.erase(it);
            } else {
                ++it;
            }
        }

        for (auto it = m_closelyWatchedAllocationsByAddress.begin(); it != m_closelyWatchedAllocationsByAddress.end(); ) {
            CloselyWatchedAllocation& alloc = it->second;
            if (alloc.deadline < now) {
                switch (alloc.state) {
                case CloselyWatchedAllocation::State::NotYetSuspicious:
                    alloc.state = CloselyWatchedAllocation::State::Suspicious;
                    alloc.deadline = now + environment.closelyWatchedAllocationsAccessMaxInterval;
                    // TODO add MemoryProtector watch
                    ++it;
                    break;
                case CloselyWatchedAllocation::State::Suspicious:
                    alloc.watchedStackTraceInfo->countLeakedCloselyWatchedAllocations++;
                    alloc.watchedStackTraceInfo->countLiveCloselyWatchedAllocations--;
                    alloc.watchedStackTraceInfo->countLiveCloselyWatchedAllocationsAllTraces--;
                    alloc.watchedStackTraceInfo->countTotalLeakedMemory += alloc.requestedSize;
                    foundLeaks.push_back({ &alloc.watchedStackTraceInfo->stackTrace, alloc.memory, alloc.requestedSize });
                    it = m_closelyWatchedAllocationsByAddress.erase(it);
                }
            } else {
                ++it;
            }
        }
        return make_tuple(m_stats, foundLeaks);
    }

    struct LeakReport {
        struct Leak {
            StackTrace* stackTrace;
            float leakRatio;
            float lostAllocationsEstimated;
            float lostBytesEstimated;
        };
        float ratioAllocationHasSuspiciousFingerprint;
        float averageStackTracesPerFingerprint;
        float ratioLeakyStacks;
        float ratioNonLeakyStacks;
        float ratioMaybeLeakyStats;
        vector<Leak> leaks;
    };

    LeakReport patrolThreadMakeLeakReport() {
        LeakReport report;
        uint32_t countFingerprints = 0;
        uint32_t countStacks = 0;
        uint32_t countLeakyStacks = 0;
        uint32_t countNonLeakyStacks = 0;
        uint32_t countMaybeLeakyStacks = 0;
        {
            lock_guard<mutex> lock(m_mutex);
            for (auto& fingerprintPair : m_suspiciousFingerprints) {
                countFingerprints++;
                for (auto& watchedTracePair: fingerprintPair.second) {
                    countStacks++;
                    WatchedStackTraceInfo& trace = watchedTracePair.second;
                    switch (trace.hasLeaks()) {
                    case Trilean::True:
                        countLeakyStacks++;
                        report.leaks.push_back({ &trace.stackTrace, trace.leakRatio(),
                                                 trace.lostAllocationsEstimated(), trace.lostBytesEstimated() });
                        break;
                    case Trilean::False:
                        countNonLeakyStacks++;
                        break;
                    case Trilean::Unknown:
                        countMaybeLeakyStacks++;
                    }
                }
            }
            report.ratioAllocationHasSuspiciousFingerprint =
                    (float) m_stats.allocationWithSuspiciousFingerprintCount / m_stats.allocationCount;
        }
        report.averageStackTracesPerFingerprint = (float) countStacks / countFingerprints;
        report.ratioLeakyStacks = (float) countLeakyStacks / countStacks;
        report.ratioNonLeakyStacks = (float) countNonLeakyStacks / countStacks;
        report.ratioMaybeLeakyStats = (float) countMaybeLeakyStacks / countStacks;

        std::sort(report.leaks.begin(), report.leaks.end(), [](const LeakReport::Leak& a, const LeakReport::Leak& b) {
            return a.lostBytesEstimated >= b.lostBytesEstimated;
        });
        return report;
    }

private:
    static AllocationTable s_allocationTable;

    mutex m_mutex;
    unordered_map<void*, LightAllocation> m_lightAllocationsByAddress;
    unordered_map<void*, CloselyWatchedAllocation> m_closelyWatchedAllocationsByAddress;
    SuspiciousFingerprintTable m_suspiciousFingerprints;
    AllocationStats m_stats;
};

#include <cstdint>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <set>
#include <algorithm>
#include <functional>
#include <time.h>
#include <cassert>
#include "callstack-fingerprint.h"
#include "environment.h"
#include "stack-trace.h"
#include "watched-stack-trace-info.h"
using namespace std;

struct Allocation {
    void* memory;
    uint32_t size;
    // This is the time when, if reached, the allocation moves to a more suspicious state:
    // If it's a LightAllocation, the fast fingerprint will be marked as suspicious.
    // If it's a CloselyWatchedAllocation, it depends on its state:
    // -> If it is in NotYetSuspicious state, it will become Suspicious.
    // -> If it is in Suspicious state, it will be declared a leak.
    uint32_t deadline;
    CallstackFingerprint callstackFingerprint;

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
};

class SuspiciousStackTracesTable : public unordered_map<StackTrace, WatchedStackTraceInfo> {
    WatchedStackTraceInfo& getOrCreate(StackTrace&& trace) {
        value_type kvIter = this->find(trace);
        if (kvIter != this->end())
            return kvIter->second;

        this->emplace(std::piecewise_construct, std::forward_as_tuple(trace), std::forward_as_tuple(std::move(trace)));
    }
};

class SuspiciousFastFingerprintTable : public unordered_multimap<CallstackFingerprint, SuspiciousStackTracesTable> {

};

class AllocationTable {
public:
    AllocationTable() {}
    static AllocationTable& instance() { return s_allocationTable; }

    void allocate(size_t size, CallstackFingerprint fingerprint, function<void*()> preferredAllocator) {

    }

    void insertNewEntry(void* memory, uint32_t size, CallstackFingerprint callstackFingerprint) {
        lock_guard<mutex> lock(m_mutex);
        size_t n = m_lightAllocationsByAddress.size();
        assert(m_lightAllocationsByAddress.find(memory) == m_lightAllocationsByAddress.end());
        Allocation* alloc = &m_lightAllocationsByAddress[memory];
        assert(m_lightAllocationsByAddress.size() == n + 1);
        alloc->memory = memory;
        alloc->size = size;
        alloc->callstackFingerprint = callstackFingerprint;
        alloc->allocationTime = time(nullptr);
        alloc->state = AllocationState::Young;
        alloc->timeout = alloc->allocationTime + environment.timeForAllocationToBecomeSuspicious;
    }

    Allocation* findEntry(void* memory) {
        lock_guard<mutex> lock(m_mutex);
        auto iter = m_lightAllocationsByAddress.find(memory);
        if (iter != m_lightAllocationsByAddress.end())
            return &iter->second;
        else
            return nullptr;
    }

    void deleteEntry(void* memory) {
        lock_guard<mutex> lock(m_mutex);
        m_lightAllocationsByAddress.erase(memory);
    }

    void reallocEntry(void* oldMemory, void* newMemory, uint32_t newSize) {
        lock_guard<mutex> lock(m_mutex);

        if (m_lightAllocationsByAddress.find(oldMemory) == m_lightAllocationsByAddress.end())
            // This allocation was not recorded, nothing to bookkeep here.
            return;

        Allocation& alloc = m_lightAllocationsByAddress[oldMemory];
        alloc.memory = newMemory;
        alloc.size = newSize;

        assert(m_lightAllocationsByAddress.find(newMemory) == m_lightAllocationsByAddress.end());
        m_lightAllocationsByAddress[newMemory] = std::move(alloc);

        m_lightAllocationsByAddress.erase(oldMemory);
    }

    // To be called by Patrol Thread only
    void updateAllocationStates() {
        for (auto it = m_lightAllocationsByAddress.begin(); it != m_lightAllocationsByAddress.end(); ++it) {

        }
    }


private:
    static AllocationTable s_allocationTable;

    unordered_map<void*, LightAllocation> m_lightAllocationsByAddress;
    unordered_map<void*, CloselyWatchedAllocation> m_closelyWatchedAllocationsByAddress;
    mutex m_mutex;
};

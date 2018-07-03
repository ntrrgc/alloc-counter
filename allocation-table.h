#include <cstdint>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <set>
#include <algorithm>
#include <time.h>
#include <cassert>
#include "callstack-fingerprint.h"
#include "environment.h"
using namespace std;

enum class AllocationState {
    Young = 0,
    OldSuspicious = 1,
    OldButUsed = 2
};

struct AllocationInfo {
    void* memory;
    uint32_t size;
    CallstackFingerprint callstackFingerprint;
    // The full traceback is only collected if the fingerprint was in the wanted list when the allocation was made.
    unique_ptr<FullTraceback> fullTraceback;
    uint32_t allocationTime;

    AllocationState state = AllocationState::Young;

    // This is the time when, if reached, the allocation moves to a more suspicious state:
    // If it is in Young, it will become OldSuspicious.
    // If it is OldSuspicious, it will be declared a leak.
    // If it is OldUsed, it will become OldSuspicious again.
    uint32_t deadline;

    AllocationInfo() {}
};



class AllocationTable {
public:
    AllocationTable() {}
    static AllocationTable& instance() { return s_allocationTable; }

    void insertNewEntry(void* memory, uint32_t size, CallstackFingerprint callstackFingerprint) {
        lock_guard<mutex> lock(m_mutex);
        size_t n = m_allocsByAddress.size();
        assert(m_allocsByAddress.find(memory) == m_allocsByAddress.end());
        AllocationInfo* alloc = &m_allocsByAddress[memory];
        assert(m_allocsByAddress.size() == n + 1);
        alloc->memory = memory;
        alloc->size = size;
        alloc->callstackFingerprint = callstackFingerprint;
        alloc->allocationTime = time(nullptr);
        alloc->state = AllocationState::Young;
        alloc->timeout = alloc->allocationTime + environment.timeForYoungToBecomeSuspicious;
    }

    AllocationInfo* findEntry(void* memory) {
        lock_guard<mutex> lock(m_mutex);
        auto iter = m_allocsByAddress.find(memory);
        if (iter != m_allocsByAddress.end())
            return &iter->second;
        else
            return nullptr;
    }

    void deleteEntry(void* memory) {
        lock_guard<mutex> lock(m_mutex);
        m_allocsByAddress.erase(memory);
    }

    void reallocEntry(void* oldMemory, void* newMemory, uint32_t newSize) {
        lock_guard<mutex> lock(m_mutex);

        if (m_allocsByAddress.find(oldMemory) == m_allocsByAddress.end())
            // This allocation was not recorded, nothing to bookkeep here.
            return;

        AllocationInfo& alloc = m_allocsByAddress[oldMemory];
        alloc.memory = newMemory;
        alloc.size = newSize;

        assert(m_allocsByAddress.find(newMemory) == m_allocsByAddress.end());
        m_allocsByAddress[newMemory] = std::move(alloc);

        m_allocsByAddress.erase(oldMemory);
    }

    class LeakSearch {
        friend class AllocationTable;
        LeakSearch(unordered_map<void*, AllocationInfo>& m_allocsByAddress, mutex& mutex, uint32_t cutOffTime)
            : m_allocsByAddress(m_allocsByAddress),
              m_cutOffTime(cutOffTime),
              m_lock(mutex),
              m_iter(m_allocsByAddress.begin())
        {}

        unordered_map<void*, AllocationInfo>& m_allocsByAddress;
        uint32_t m_cutOffTime;
        lock_guard<mutex> m_lock;
        unordered_map<void*, AllocationInfo>::iterator m_iter;
        bool first_iteration = true;

    public:
        // The returned AllocationInfo only lives until the next call!
        const AllocationInfo* next() {
            if (!first_iteration && m_iter != m_allocsByAddress.end()) {
                // Delete the element containing the previous leak.
                m_iter = m_allocsByAddress.erase(m_iter);
            }

            first_iteration = false;
            for (; m_iter != m_allocsByAddress.end(); ++m_iter) {
                AllocationInfo* alloc = &(*m_iter).second;
                if (alloc->allocationTime < m_cutOffTime) {
                    free(alloc->memory);
                    return alloc;
                }
            }
            return nullptr; // no more leaks
        }
    };
    friend class LeakSearch;

    // guaranteed copy ellision requires gcc 7 (C++17), next best thing is unique_ptr
    unique_ptr<LeakSearch> findLeakedEntries(uint32_t allocationsBecomeLeaksAfterSeconds) {
        return unique_ptr<LeakSearch>(new LeakSearch(m_allocsByAddress, m_mutex, time(0) - allocationsBecomeLeaksAfterSeconds));
    }

    // To be called by Patrol Thread only
    void updateAllocationStates() {
        for (auto it = m_allocsByAddress.begin(); it != m_allocsByAddress.end(); ++it) {

        }
    }


private:
    static AllocationTable s_allocationTable;

    unordered_map<void*, AllocationInfo> m_allocsByAddress;
    mutex m_mutex;
};

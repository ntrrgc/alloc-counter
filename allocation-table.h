#include <cstdint>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <time.h>
#include "callstack-fingerprint.h"
using namespace std;

struct AllocationInfo {
    void* memory;
    uint32_t size;
    CallstackFingerprint callstackFingerprint;
    uint32_t time;
};

class AllocationTable {
public:
    AllocationTable() {}
    static AllocationTable& instance() { return s_allocationTable; }

    void insertNewEntry(void* memory, uint32_t size, CallstackFingerprint callstackFingerprint) {
        lock_guard<mutex> lock(m_mutex);
        AllocationInfo* alloc = &m_allocsByAddress[memory];
        alloc->memory = memory;
        alloc->size = size;
        alloc->callstackFingerprint = callstackFingerprint;
        alloc->time = time(nullptr);
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

        AllocationInfo alloc = m_allocsByAddress[oldMemory];
        alloc.memory = newMemory;
        alloc.size = newSize;

        m_allocsByAddress.erase(oldMemory);
        m_allocsByAddress[newMemory] = alloc;
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
                if (alloc->time < m_cutOffTime) {
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


private:
    static AllocationTable s_allocationTable;

    unordered_map<void*, AllocationInfo> m_allocsByAddress;
    mutex m_mutex;
};

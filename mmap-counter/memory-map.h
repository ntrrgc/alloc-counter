#include <memory>
#include <map>
#include <mutex>
#include "interned-stack-trace.h"

#ifndef MMAP_COUNTER_TESTS
#define MMAP_COUNTER_PRIVATE private
#else
#define MMAP_COUNTER_PRIVATE public
#endif

struct MMapAllocation {
    explicit MMapAllocation(intptr_t originalStart, intptr_t originalSize)
        : originalStart(originalStart), originalSize(originalSize)
    {}

    InternedStackTrace stackTrace;
    intptr_t originalStart;
    size_t originalSize;

    intptr_t originalEnd() const { return originalStart + originalSize; }
};

struct MemorySlice {
    explicit MemorySlice(intptr_t start, size_t size, std::shared_ptr<MMapAllocation> allocation)
        : start(start), size(size), allocation(std::move(allocation))
    {}
    intptr_t start;
    size_t size;
    std::shared_ptr<MMapAllocation> allocation;

    intptr_t end() const { return start + size; }
};

class MemoryMap: public std::map<intptr_t, MemorySlice> {
public:
    void registerMap(MMapAllocation&& allocation) {
        std::lock_guard<std::mutex> lock(m_mutex);
        // Initially all mmap() allocations create one MemorySlice, which may be sliced if a partial munmap() is made.
        intptr_t start = allocation.originalStart;
        size_t size = allocation.originalSize;
        insert(std::make_pair(start, MemorySlice(start, size, std::make_shared<MMapAllocation>(std::move(allocation)))));
    }

    bool registerUnmap(intptr_t start, size_t size) {
        // Returns true if known anonymous memory is unmapped.
        std::lock_guard<std::mutex> lock(m_mutex);
        intptr_t end = start + size;
        splitAt(start);
        splitAt(end);

        auto eraseStart = lower_bound(start);
        auto eraseEnd = lower_bound(end);
        erase(eraseStart, eraseEnd);
        return eraseStart != eraseEnd;
    }

MMAP_COUNTER_PRIVATE:
    void splitAt(intptr_t pointer) {
        // upper_bound() finds the first element that starts STRICTLY after pointer.
        // We are interested in the element that is immediately before that one: if it contains
        // pointer, we will split it.
        auto iterNext = upper_bound(pointer);
        if (iterNext == begin())
            return;
        auto iter = std::prev(iterNext);
        if (iter == end())
            return;

        MemorySlice& greatestElementThatStartsBeforePointer = iter->second;
        assert(greatestElementThatStartsBeforePointer.start <= pointer);
        if (pointer == greatestElementThatStartsBeforePointer.start
                || pointer >= greatestElementThatStartsBeforePointer.end()) {
            return;
        }

        // Let's split
        intptr_t newSliceEnd = greatestElementThatStartsBeforePointer.end();
        greatestElementThatStartsBeforePointer.size = pointer - greatestElementThatStartsBeforePointer.start;
        insert(std::make_pair(pointer, MemorySlice(pointer, newSliceEnd - pointer, greatestElementThatStartsBeforePointer.allocation)));
    }

    std::mutex m_mutex;
};

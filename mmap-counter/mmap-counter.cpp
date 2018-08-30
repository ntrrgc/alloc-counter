#include <sys/mman.h>
#include <dlfcn.h>
#include <stdio.h>
#include <unordered_map>
#include <map>
#include <memory>
#include <cassert>
#include <mutex>
#include <unistd.h>
#include "stack-trace.h"

struct Deref {
    struct Hash {
        template <typename T>
        std::size_t operator() (std::shared_ptr<T> const &p) const {
            return std::hash<T>()(*p);
        }
    };
    struct Compare {
        template <typename T>
        size_t operator() (std::shared_ptr<T> const &a,
                           std::shared_ptr<T> const &b) const {
            return *a == *b;
        }
    };
};

class InternedStackTrace
{
public:
    explicit InternedStackTrace() {
        StackTrace stackTrace;
        auto iter = s_stackTraces.find(stackTrace.hash());
        if (iter != s_stackTraces.end()) {
            m_stackTrace = iter->second;
        } else {
            m_stackTrace = std::make_shared<StackTrace>(std::move(stackTrace));
            assert(stackTrace.hash() == m_stackTrace->hash());
            s_stackTraces[m_stackTrace->hash()] = m_stackTrace;
        }
    }
    ~InternedStackTrace() {
        assert(!m_stackTrace || m_stackTrace.use_count() >= 2);
        if (m_stackTrace && m_stackTrace.use_count() == 2) {
            s_stackTraces.erase(m_stackTrace->hash());
            assert(m_stackTrace.use_count() == 1);
        }
    }

    const StackTrace& get() const {
        return *m_stackTrace;
    }

private:
    std::shared_ptr<StackTrace> m_stackTrace;

    static std::unordered_map<size_t, std::shared_ptr<StackTrace>> s_stackTraces;
};

std::unordered_map<size_t, std::shared_ptr<StackTrace>> InternedStackTrace::s_stackTraces;

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
    bool operator<(const MemorySlice& other) const {
        return start < other.start;
    }
};

class MemoryMap: public std::map<intptr_t, MemorySlice> {
public:
    void registerAllocation(MMapAllocation&& allocation) {
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

private:
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

        MemorySlice& greatestElementThatStartsBeforePointer = std::prev(iterNext)->second;
        assert(greatestElementThatStartsBeforePointer.start < pointer);
        if (pointer >= greatestElementThatStartsBeforePointer.end())
            return;

        // Let's split
        intptr_t newSliceEnd = greatestElementThatStartsBeforePointer.end();
        greatestElementThatStartsBeforePointer.size = pointer - greatestElementThatStartsBeforePointer.start;
        insert(std::make_pair(pointer, MemorySlice(pointer, newSliceEnd - pointer, greatestElementThatStartsBeforePointer.allocation)));
    }

    std::mutex m_mutex;
};

static uint32_t roundUpToPageMultiple(size_t size) {
    const size_t pageSize = sysconf(_SC_PAGE_SIZE);
    return (size + (pageSize - 1)) & ~(pageSize - 1);
}

static void* (*real_mmap)(void*, size_t, int, int, int, __off_t) = nullptr;
static int (*real_munmap)(void*, size_t) = nullptr;

extern "C" {

static MemoryMap memoryMap;

__attribute__((visibility("default")))
void* mmap(void *addr, size_t len, int prot, int flags, int fd, __off_t offset) {
    if (!real_mmap) {
        real_mmap = (void*(*)(void*, size_t, int, int, int, __off_t)) dlsym(RTLD_NEXT, "mmap");
    }
    void* ret = real_mmap(addr, len, prot, flags, fd, offset);
    if (ret != nullptr && fd == MAP_ANONYMOUS) {
        fprintf(stderr, "MAP:   %p (%zu bytes)\n", ret, len);
        intptr_t start = reinterpret_cast<intptr_t>(ret);
        memoryMap.registerAllocation(MMapAllocation(start, roundUpToPageMultiple(len)));
    }
    return ret;
}

__attribute__((visibility("default")))
int munmap(void *addr, size_t length) {
    if (!real_munmap) {
        real_munmap = (int(*)(void*, size_t)) dlsym(RTLD_NEXT, "munmap");
    }
    int ret = real_munmap(addr, length);
    if (ret == 0) {
        intptr_t start = reinterpret_cast<intptr_t>(addr);
        const size_t pageSize = sysconf(_SC_PAGE_SIZE);
        assert((start & (pageSize - 1)) == 0); // addr is multiple of pageSize
        if (memoryMap.registerUnmap(start, roundUpToPageMultiple(length))) {
            fprintf(stderr, "UNMAP: %p (%zu bytes)\n", addr, length);
        }
    } else {
        fprintf(stderr, "UNMAP FAILED: %p (%zu bytes)\n", addr, length);
    }
    return ret;
}

}

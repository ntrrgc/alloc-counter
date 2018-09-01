#include <sys/mman.h>
#include <dlfcn.h>
#include <stdio.h>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <memory>
#include <mutex>
#include <unistd.h>
#include <cassert>
#include <fstream>
#include <mutex>
#include "memory-map.h"
#include "library-context.h"

#ifdef MMAP_COUNTER_LOG_ENABLED
#define LOG(...) fprintf(stderr, __VA_ARGS__)
#else
#define LOG(...)
#endif

static uint32_t roundUpToPageMultiple(size_t size) {
    const size_t pageSize = sysconf(_SC_PAGE_SIZE);
    return (size + (pageSize - 1)) & ~(pageSize - 1);
}

static double getTime() {
    timespec tv;
    clock_gettime(CLOCK_MONOTONIC, &tv);
    return tv.tv_sec + tv.tv_nsec / 1e9;
}

static void* (*real_mmap)(void*, size_t, int, int, int, off_t) = nullptr;
static int (*real_munmap)(void*, size_t) = nullptr;

extern "C" {

static MemoryMap memoryMap;
static std::mutex wrappedMmapMutex;
static std::unordered_map<size_t, size_t> knownStackTraceHashToId;

std::pair<size_t, bool> getOrAssignStackId(const StackTrace& stackTrace) {
    auto pair = knownStackTraceHashToId.insert(std::make_pair(stackTrace.hash(), knownStackTraceHashToId.size() + 1));
    return std::make_pair(pair.first->second, pair.second);
}

static ofstream& openEventLogFile() {
    static ofstream logFile("/tmp/mmap-event-log", ofstream::trunc);
    return logFile;
}

static ofstream& openStackLogFile() {
    static ofstream logFile("/tmp/mmap-stack-log", ofstream::trunc);
    return logFile;
}

__attribute__((visibility("default")))
void* mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset) {
    LOG("mmap\n");
    if (!real_mmap) {
#ifdef __USE_FILE_OFFSET64
        real_mmap = (void*(*)(void*, size_t, int, int, int, off_t)) dlsym(RTLD_NEXT, "mmap64");
#else
        real_mmap = (void*(*)(void*, size_t, int, int, int, off_t)) dlsym(RTLD_NEXT, "mmap");
#endif
    }
    LOG("real_mmap = %p\n", real_mmap);
    void* ret = real_mmap(addr, len, prot, flags, fd, offset);
    LOG("ret = %p\n", ret);
    if (LibraryContext::inLibrary()) {
        LOG("return from mmap (in library)\n");
        return ret;
    }

    LibraryContext ctx;
    if (ret != nullptr && flags & MAP_ANONYMOUS) {
        std::lock_guard<std::mutex> lock(wrappedMmapMutex);
        intptr_t start = reinterpret_cast<intptr_t>(ret);
        MMapAllocation allocation(start, roundUpToPageMultiple(len));
        auto pair = getOrAssignStackId(allocation.stackTrace.get());
        size_t stackTraceId = pair.first;
        bool stackTraceIsNew = pair.second;
        if (stackTraceIsNew) {
            openStackLogFile() << "New stack trace: " << stackTraceId << "\n"
                               << allocation.stackTrace.get() << endl;
        }
        openEventLogFile() << getTime() << " MAP: " << ret << " ("
                           << len << " bytes) stackTrace=" << stackTraceId << endl;
        memoryMap.registerMap(std::move(allocation));
    }
    LOG("return from mmap\n");
    return ret;
}

__attribute__((visibility("default")))
int munmap(void *addr, size_t length) {
    LOG("munmap\n");
    if (!real_munmap) {
        real_munmap = (int(*)(void*, size_t)) dlsym(RTLD_NEXT, "munmap");
    }
    LOG("real_munmap = %p\n", real_munmap);
    int ret = real_munmap(addr, length);
    LOG("ret = %d\n", ret);
    if (LibraryContext::inLibrary()) {
        LOG("return from munmap (in library)\n");
        return ret;
    }

    LibraryContext ctx;
    std::lock_guard<std::mutex> lock(wrappedMmapMutex);
    if (ret == 0) {
        intptr_t start = reinterpret_cast<intptr_t>(addr);
        const size_t pageSize = sysconf(_SC_PAGE_SIZE);
        assert((start & (pageSize - 1)) == 0); // addr is multiple of pageSize
        if (memoryMap.registerUnmap(start, roundUpToPageMultiple(length))) {
            openEventLogFile() << getTime() << " UNMAP: " << addr << " (" << length << " bytes)" << endl;
        }
    } else {
        openEventLogFile() << getTime() << " ERROR: UNMAP FAILED: " << addr << " (" << length << " bytes)" << endl;
    }
    LOG("return from munmap\n");
    return ret;
}

}

#include <sys/mman.h>
#include <dlfcn.h>
#include <stdio.h>
#include <unordered_map>
#include <map>
#include <memory>
#include <mutex>
#include <unistd.h>
#include <cassert>
#include "memory-map.h"

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
        memoryMap.registerMap(MMapAllocation(start, roundUpToPageMultiple(len)));
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

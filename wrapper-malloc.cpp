#include "library-context.h"
#include "comm-memory.h"
#include "allocation-stats.h"
#include "allocation-table.h"
#include <strings.h>
#include <dlfcn.h>
#include <csignal>

extern "C" {

static void* (*real_malloc)(size_t) = nullptr;
static void  (*real_free)(void*) = nullptr;
static void* (*real_calloc)(size_t, size_t) = nullptr;
static void* (*real_realloc)(void*, size_t) = nullptr;
static void* (*real_reallocarray)(void*, size_t, size_t) = nullptr;

static int   (*real_posix_memalign)(void**, size_t, size_t) = nullptr;
static void* (*real_aligned_alloc)(size_t, size_t) = nullptr;
static void* (*real_valloc)(size_t) = nullptr;
static void* (*real_memalign)(size_t, size_t) = nullptr;
static void* (*real_pvalloc)(size_t) = nullptr;

#ifdef __arm__
#define STACK_REGISTER "sp"
#else // x86_64
#define STACK_REGISTER "rsp"
#endif

thread_local bool insideRealloc = false;

static void instrumentAllocation(void* memory, size_t size, void* returnAddress) {
    if (LibraryContext::inLibrary() || getWatchState() == WatchState::NotWatching)
        return;

    if (insideRealloc)
        raise(SIGABRT);

    LibraryContext ctx;

    AllocationStats::instance().ensureEnabled();
    ++AllocationStats::instance().allocationCount;

    register void* stackPointer asm (STACK_REGISTER);
    CallstackFingerprint fingerprint = computeCallstackFingerprint(stackPointer, returnAddress, size);
    AllocationTable::instance().insertNewEntry(memory, size, fingerprint);
}

static void instrumentReallocation(void* oldMemory, void* newMemory, size_t newSize) {
    if (LibraryContext::inLibrary() || getWatchState() == WatchState::NotWatching)
        return;

    LibraryContext ctx;
    AllocationTable::instance().reallocEntry(oldMemory, newMemory, newSize);
    AllocationStats::instance().ensureEnabled();
    ++AllocationStats::instance().reallocCount;
}

static void instrumentFree(void* memory) {
    if (LibraryContext::inLibrary() || getWatchState() == WatchState::NotWatching)
        return;

    LibraryContext ctx;
    AllocationTable::instance().deleteEntry(memory);
    AllocationStats::instance().ensureEnabled();
    ++AllocationStats::instance().freeCount;
}


void* malloc(size_t size) {
    if (!real_malloc) {
        real_malloc = (void*(*)(size_t)) dlsym(RTLD_NEXT, "malloc");
    }

    void* memory = real_malloc(size);
    if (!memory)
        return nullptr;
    instrumentAllocation(memory, size, __builtin_return_address(0));
    return memory;
}

void* calloc(size_t numMembers, size_t memberSize) {
    if (!real_calloc) {
        // calloc() is a bit tricky to wrap because dlsym() calls it.
        // Fortunately, it's not for any important stuff and it can accept a NULL pointer.
        // http://blog.bigpixel.ro/interposing-calloc-on-linux/
        static bool alreadyInDlsymCall = false;
        if (alreadyInDlsymCall)
            return nullptr;
        alreadyInDlsymCall = true;
        real_calloc = (void*(*)(size_t, size_t)) dlsym(RTLD_NEXT, "calloc");
    }

    void* memory = real_calloc(numMembers, memberSize);
    if (!memory)
        return nullptr;
    instrumentAllocation(memory, numMembers * memberSize, __builtin_return_address(0));
    return memory;
}

int posix_memalign(void **memptr, size_t alignment, size_t size) {
    if (!real_posix_memalign) {
        real_posix_memalign = (int(*)(void**, size_t, size_t)) dlsym(RTLD_NEXT, "posix_memalign");
    }

    int ret = real_posix_memalign(memptr, alignment, size);
    if (ret == 0) { // Success
        instrumentAllocation(*memptr, size, __builtin_return_address(0));
    }
    return ret;
}

void *aligned_alloc(size_t alignment, size_t size) {
    if (!real_aligned_alloc) {
        real_aligned_alloc = (void*(*)(size_t, size_t)) dlsym(RTLD_NEXT, "aligned_alloc");
    }

    void* memory = real_aligned_alloc(alignment, size);
    if (!memory)
        return nullptr;
    instrumentAllocation(memory, size, __builtin_return_address(0));
    return memory;
}

void *valloc(size_t size) {
    if (!real_valloc) {
        real_valloc = (void*(*)(size_t)) dlsym(RTLD_NEXT, "valloc");
    }

    void* memory = real_valloc(size);
    if (!memory)
        return nullptr;
    instrumentAllocation(memory, size, __builtin_return_address(0));
    return memory;
}

void *memalign(size_t alignment, size_t size) {
    if (!real_memalign) {
        real_memalign = (void*(*)(size_t, size_t)) dlsym(RTLD_NEXT, "memalign");
    }

    void* memory = real_memalign(alignment, size);
    if (!memory)
        return nullptr;
    instrumentAllocation(memory, size, __builtin_return_address(0));
    return memory;
}

void *pvalloc(size_t size) {
    if (!real_pvalloc) {
        real_pvalloc = (void*(*)(size_t)) dlsym(RTLD_NEXT, "pvalloc");
    }

    void* memory = real_pvalloc(size);
    if (!memory)
        return nullptr;
    instrumentAllocation(memory, size, __builtin_return_address(0));
    return memory;
}

void free(void* memory) {
    if (!real_free)
        real_free = (void(*)(void*)) dlsym(RTLD_NEXT, "free");

    real_free(memory);
    if (!memory)
        return;

    instrumentFree(memory);
}

void* realloc(void* oldMemory, size_t newSize) {
    if (!real_realloc)
        real_realloc = (void*(*)(void*, size_t)) dlsym(RTLD_NEXT, "realloc");

    insideRealloc = true;
    void* newMemory = real_realloc(oldMemory, newSize);
    insideRealloc = false;

    // Surprising fact: realloc() is multipurpose (see man realloc)
    if (oldMemory && newSize == 0) {
        instrumentFree(oldMemory);
    } else if (oldMemory && newMemory && newSize > 0) {
        instrumentReallocation(oldMemory, newMemory, newSize);
    } else if (!oldMemory && newMemory) {
        instrumentAllocation(newMemory, newSize, __builtin_return_address(0));
    }
    return newMemory;
}

void* reallocarray(void* oldMemory, size_t newNumElements, size_t newElementSize) {
    if (!real_reallocarray)
        real_reallocarray = (void*(*)(void*, size_t, size_t)) dlsym(RTLD_NEXT, "reallocarray");

    insideRealloc = true;
    void* newMemory = real_reallocarray(oldMemory, newNumElements, newElementSize);
    insideRealloc = false;

    // Surprising fact: realloc() is multipurpose (see man realloc)
    size_t newSize = newNumElements * newElementSize;
    if (oldMemory && newSize == 0) {
        instrumentFree(oldMemory);
    } else if (oldMemory && newMemory && newSize > 0) {
        instrumentReallocation(oldMemory, newMemory, newSize);
    } else if (!oldMemory && newMemory) {
        instrumentAllocation(newMemory, newSize, __builtin_return_address(0));
    }
    return newMemory;
}

}

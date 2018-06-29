#include "library-context.h"
#include "comm-memory.h"
#include "allocation-stats.h"
#include "allocation-table.h"
#include <strings.h>
#include <dlfcn.h>

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

static void instrumentAllocation(void* memory, size_t size, void* returnAddress) {
    if (LibraryContext::inLibrary() || getWatchState() == WatchState::NotWatching)
        return;

    LibraryContext ctx;

    AllocationStats::instance().ensureEnabled();
    ++AllocationStats::instance().allocationCount;

    register void* stackPointer asm (STACK_REGISTER);
    CallstackFingerprint fingerprint = computeCallstackFingerprint(stackPointer, returnAddress, size);
    AllocationTable::instance().insertNewEntry(memory, size, fingerprint);
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

void free(void* memory) {
    if (!real_free)
        real_free = (void(*)(void*)) dlsym(RTLD_NEXT, "free");

    real_free(memory);
    if (!memory)
        return;

    if (LibraryContext::inLibrary() || getWatchState() == WatchState::NotWatching)
        return;

    LibraryContext ctx;
    AllocationTable::instance().deleteEntry(memory);
    ++AllocationStats::instance().freeCount;
}

static void instrumentReallocation(void* oldMemory, void* newMemory, size_t newSize) {
    if (LibraryContext::inLibrary() || getWatchState() == WatchState::NotWatching)
        return;

    LibraryContext ctx;
    AllocationTable::instance().reallocEntry(oldMemory, newMemory, newSize);
    ++AllocationStats::instance().reallocCount;
}

void* realloc(void* oldMemory, size_t newSize) {
    if (!real_realloc)
        real_realloc = (void*(*)(void*, size_t)) dlsym(RTLD_NEXT, "realloc");

    void* newMemory = real_realloc(oldMemory, newSize);
    if (!newMemory)
        return nullptr;
    instrumentReallocation(oldMemory, newMemory, newSize);
    return newMemory;
}

void* reallocarray(void* oldMemory, size_t newNumElements, size_t newElementSize) {
    if (!real_reallocarray)
        real_reallocarray = (void*(*)(void*, size_t, size_t)) dlsym(RTLD_NEXT, "reallocarray");

    void* newMemory = real_reallocarray(oldMemory, newNumElements, newElementSize);
    if (!newMemory)
        return nullptr;
    instrumentReallocation(oldMemory, newMemory, newNumElements * newElementSize);
    return newMemory;
}



}

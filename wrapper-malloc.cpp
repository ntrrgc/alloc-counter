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

CallstackFingerprint inline __attribute__((always_inline)) makeCallstackFingerprint(uint32_t allocationSize) {
    // This function must be always_inline so that we can get the return address of malloc(), not of this function.
    register void* stackPointer asm (STACK_REGISTER);
    return computeCallstackFingerprint(stackPointer, __builtin_return_address(0), allocationSize);
}

void* malloc(size_t size) {
    if (!real_malloc) {
        real_malloc = (void*(*)(size_t)) dlsym(RTLD_NEXT, "malloc");
    }

    return AllocationTable::instance().instrumentedAllocate(size, AllocationTable::NoAlignment, makeCallstackFingerprint(size), [size]() {
        return real_malloc(size);
    }, AllocationTable::ZeroFill::Unnecessary);
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

    size_t size = numMembers * memberSize;
    return AllocationTable::instance().instrumentedAllocate(size, AllocationTable::NoAlignment, makeCallstackFingerprint(size), [numMembers, memberSize]() {
        return real_calloc(numMembers, memberSize);
    }, AllocationTable::ZeroFill::Needed);
}

int posix_memalign(void **memptr, size_t alignment, size_t size) {
    if (!real_posix_memalign) {
        real_posix_memalign = (int(*)(void**, size_t, size_t)) dlsym(RTLD_NEXT, "posix_memalign");
    }

    int errorCode = 0; // Success
    void *memory = AllocationTable::instance().instrumentedAllocate(size, alignment, makeCallstackFingerprint(size), [size, alignment, &errorCode]() {
        void* pointerHolder = nullptr;
        errorCode = real_posix_memalign(&pointerHolder, alignment, size);
        return pointerHolder;
    }, AllocationTable::ZeroFill::Unnecessary);

    if (memory) {
        *memptr = memory;
    }
    return errorCode;
}

void *aligned_alloc(size_t alignment, size_t size) {
    if (!real_aligned_alloc) {
        real_aligned_alloc = (void*(*)(size_t, size_t)) dlsym(RTLD_NEXT, "aligned_alloc");
    }

    return AllocationTable::instance().instrumentedAllocate(size, alignment, makeCallstackFingerprint(size), [alignment, size]() {
        return real_aligned_alloc(alignment, size);
    }, AllocationTable::ZeroFill::Unnecessary);
}

void *valloc(size_t size) {
    if (!real_valloc) {
        real_valloc = (void*(*)(size_t)) dlsym(RTLD_NEXT, "valloc");
    }

    return AllocationTable::instance().instrumentedAllocate(size, environment.pageSize, makeCallstackFingerprint(size), [size]() {
        return real_valloc(size);
    }, AllocationTable::ZeroFill::Unnecessary);
}

void *memalign(size_t alignment, size_t size) {
    if (!real_memalign) {
        real_memalign = (void*(*)(size_t, size_t)) dlsym(RTLD_NEXT, "memalign");
    }

    return AllocationTable::instance().instrumentedAllocate(size, alignment, makeCallstackFingerprint(size), [alignment, size]() {
        return real_memalign(alignment, size);
    }, AllocationTable::ZeroFill::Unnecessary);
}

void *pvalloc(size_t size) {
    if (!real_pvalloc) {
        real_pvalloc = (void*(*)(size_t)) dlsym(RTLD_NEXT, "pvalloc");
    }

    return AllocationTable::instance().instrumentedAllocate(size, environment.pageSize, makeCallstackFingerprint(size), [size]() {
        return real_pvalloc(size);
    }, AllocationTable::ZeroFill::Unnecessary);
}

void free(void* memory) {
    if (!real_free)
        real_free = (void(*)(void*)) dlsym(RTLD_NEXT, "free");

    AllocationTable::instance().instrumentedFree(memory, [memory]() {
        real_free(memory);
    });
}

void* realloc(void* oldMemory, size_t newSize) {
    if (!real_realloc)
        real_realloc = (void*(*)(void*, size_t)) dlsym(RTLD_NEXT, "realloc");

    // Surprising fact: realloc() is multipurpose (see man realloc)
    if (!oldMemory) {
        return AllocationTable::instance().instrumentedAllocate(newSize, AllocationTable::NoAlignment, makeCallstackFingerprint(newSize), [newSize]() {
            return real_realloc(nullptr, newSize);
        }, AllocationTable::ZeroFill::Unnecessary);
    } else if (newSize == 0) {
        void* ret;
        AllocationTable::instance().instrumentedFree(oldMemory, [oldMemory, &ret]() {
            // man realloc: If size was equal to 0, either NULL or a pointer suitable to be passed to free() is returned.
            ret = real_realloc(oldMemory, 0);
        });
        return ret;
    } else {
        return AllocationTable::instance().instrumentedReallocate(oldMemory, newSize, [oldMemory, newSize]() {
            return real_realloc(oldMemory, newSize);
        });
    }
}

void* reallocarray(void* oldMemory, size_t newNumElements, size_t newElementSize) {
    if (!real_reallocarray)
        real_reallocarray = (void*(*)(void*, size_t, size_t)) dlsym(RTLD_NEXT, "reallocarray");

    size_t newSize = newNumElements * newElementSize;

    // Surprising fact: realloc() is multipurpose (see man realloc)
    if (!oldMemory) {
        return AllocationTable::instance().instrumentedAllocate(newSize, AllocationTable::NoAlignment,
                                                                makeCallstackFingerprint(newSize), [newNumElements, newElementSize]() {
            return real_reallocarray(nullptr, newNumElements, newElementSize);
        }, AllocationTable::ZeroFill::Unnecessary);
    } else if (newSize == 0) {
        void* ret;
        AllocationTable::instance().instrumentedFree(oldMemory, [oldMemory, newNumElements, newElementSize, &ret]() {
            // man realloc: If size was equal to 0, either NULL or a pointer suitable to be passed to free() is returned.
            ret = real_reallocarray(oldMemory, newNumElements, newElementSize);
        });
        return ret;
    } else {
        return AllocationTable::instance().instrumentedReallocate(oldMemory, newSize, [oldMemory, newNumElements, newElementSize]() {
            return real_reallocarray(oldMemory, newNumElements, newElementSize);
        });
    }
}

}

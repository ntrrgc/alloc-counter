#include <strings.h>
#include <dlfcn.h>
#include <csignal>

extern "C" {

void* malloc(size_t size) {
    if (!real_malloc) {
        real_malloc = (void*(*)(size_t)) dlsym(RTLD_NEXT, "malloc");
    }

    return AllocationTable::instance().instrumentedAllocate(size, AllocationTable::NoAlignment, makeCallstackFingerprint(size), [size]() {
        return real_malloc(size);
    }, AllocationTable::ZeroFill::Unnecessary);
}

}

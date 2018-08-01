#include "library-context.h"
#include "patrol-thread.h"
#include "comm-memory.h"

__attribute__((constructor)) void allocCounterInit(void) {
    // Note: initRealMallocFunctions() does not need to be called here, by the time this function is called malloc
    // has already been used.

    LibraryContext ctx;

    initCommMemory();
    PatrolThread::spawn();
}

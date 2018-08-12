#pragma once
#include <cassert>

// The presence of this object ensures that unwrapped malloc is used in the holding thread
// until LibraryContext goes out of scope and is destroyed.
class LibraryContext {
public:
    LibraryContext() {
        assert(!__inLibrary);
        __inLibrary = true;
    }
    ~LibraryContext() {
        assert(inLibrary());
        __inLibrary = false;
    }

    static thread_local bool __inLibrary;

    static bool inLibrary() { return __inLibrary; }
};

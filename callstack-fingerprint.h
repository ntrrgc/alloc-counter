#pragma once
#include <cstdint>
#include <set>
using namespace std;

typedef uint32_t CallstackFingerprint;

// This is lossy in 64-bit systems.
uint32_t inline pointerToInt(void * pointer) {
    return static_cast<uint32_t>(reinterpret_cast<unsigned long>(pointer));
}

inline CallstackFingerprint computeCallstackFingerprint(void* stackPointer, void* returnAddress, uint32_t allocationSize) {
    uint32_t sizeClass;
    if (allocationSize < 100)
        sizeClass = allocationSize;
    else if (allocationSize < 2048)
        sizeClass = 769; // arbitrary value (it just needs to be different than other size classes)
    else
        sizeClass = 49157;

    return (pointerToInt(stackPointer) << 1)
        ^ pointerToInt(returnAddress)
        ^ (sizeClass * 786433);
}

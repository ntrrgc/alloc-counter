#pragma once
#include <cstdint>

typedef uint32_t CallstackFingerprint;

// This is lossy in 64-bit systems.
uint32_t inline pointerToInt(void * pointer) {
    return static_cast<uint32_t>(reinterpret_cast<unsigned long>(pointer));
}

inline CallstackFingerprint computeCallstackFingerprint(void* stackPointer, void* returnAddress, uint32_t allocationSize) {
    return (pointerToInt(stackPointer) << 1)
        ^ pointerToInt(returnAddress)
        ^ (allocationSize * 786433);
}

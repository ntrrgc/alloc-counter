#include "stack-trace.h"
#define UNW_LOCAL_ONLY
#include <libunwind.h>
#include <dlfcn.h>
#include <cassert>

static_assert(sizeof(void*) == sizeof(unw_word_t), "In this machine the register size and pointer size don't match.");

// For best performance, set this as environment variable: UNW_ARM_UNWIND_METHOD=UNW_ARM_METHOD_EXIDX

StackTrace::StackTrace(int numSkipCalls) noexcept {
    unw_cursor_t cursor;
    unw_context_t context;

    unw_getcontext(&context);
    unw_init_local(&cursor, &context);

    m_hash = 0;
    std::hash<unw_word_t> hashWord;
    while (unw_step(&cursor)) {
        unw_word_t ip;
        unw_get_reg(&cursor, UNW_REG_IP, &ip);

        if (numSkipCalls == 0) {
            m_returnAddresses.push_back((void*) ip);
            m_hash = (m_hash << 1) ^ hashWord(ip);
        } else {
            numSkipCalls--;
        }
    }
}

bool StackTrace::operator==(const StackTrace &other) const noexcept
{
    return m_returnAddresses == other.m_returnAddresses;
}

size_t offset(void* base, void* pointer) {
    assert(pointer >= base);
    return (char*)pointer - (char*)base;
}

ostream &operator<<(ostream &os, const StackTrace &st)
{
    int frameNumber = st.m_returnAddresses.size();
    for (void* returnAddress : st.m_returnAddresses) {
        // TODO unset thumb bit?
        Dl_info info;
        dladdr(returnAddress, &info);

        os << "    #" << frameNumber << " " << returnAddress;
        if (info.dli_sname) {
            os << " in " << info.dli_sname << "+" << offset(info.dli_saddr, returnAddress);
        }
        if (info.dli_fname) {
            os << " (" << info.dli_fname << "+0x" << hex << offset(info.dli_fbase, returnAddress) << dec << ")";
        }
        os << endl;
        frameNumber--;
    }
    return os << dec;
}

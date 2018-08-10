#include "stack-trace.h"
#define UNW_LOCAL_ONLY
#include <libunwind.h>
#include <dlfcn.h>
#include <cassert>
#include "environment.h"

static_assert(sizeof(unw_word_t) >= sizeof(void*), "unw_word_t should be able to fit a pointer");

// For best performance, set this as environment variable: UNW_ARM_UNWIND_METHOD=UNW_ARM_METHOD_EXIDX

StackTrace::StackTrace(int numSkipCalls) noexcept {
    static bool firstTime = true;
    if (firstTime) {
        firstTime = false;
        unw_set_caching_policy(unw_local_addr_space, UNW_CACHE_GLOBAL);
    }

    unw_cursor_t cursor;
    unw_context_t context;

    unw_getcontext(&context);
    unw_init_local(&cursor, &context);

    m_hash = 0;
    while (unw_step(&cursor)) {
        unw_word_t ip;
        unw_get_reg(&cursor, UNW_REG_IP, &ip);

//            m_returnAddresses.push_back(reinterpret_cast<void*>(ip));
        m_hash = (m_hash << 1) ^ static_cast<unsigned int>(ip);
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
    int frameNumber = 0;
    for (void* returnAddress : st.m_returnAddresses) {
        Dl_info info;
        int dladdrSuccess = dladdr(returnAddress, &info);

        os << "    #" << frameNumber << " " << returnAddress;
        if (!dladdrSuccess) {
            // This supposed return pointer does not actually point to a code section of any executable.
            os << " [GARBAGE]";
        }
        if (dladdrSuccess && info.dli_sname) {
            os << " in " << info.dli_sname << "+" << offset(info.dli_saddr, returnAddress);
        }
        if (dladdrSuccess && info.dli_fname) {
            os << " (" << info.dli_fname << ":" << environment.archName << "+0x"
               << hex << offset(info.dli_fbase, returnAddress) << dec << ")";
        }
        os << endl;
        frameNumber++;
    }
    return os << dec;
}

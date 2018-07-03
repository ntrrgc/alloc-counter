#include "stack-trace.h"
#define UNW_LOCAL_ONLY
#include <libunwind.h>

// For best performance, set this as environment variable: UNW_ARM_UNWIND_METHOD=UNW_ARM_METHOD_EXIDX

#include <cxxabi.h>

StackTrace::StackTrace(int numSkipCalls) {
    unw_cursor_t cursor;
    unw_context_t context;

    unw_getcontext(&context);
    unw_init_local(&cursor, &context);

    while (unw_step(&cursor)) {
        unw_word_t ip;
        unw_get_reg(&cursor, UNW_REG_IP, &ip);

        if (numSkipCalls == 0)
            m_returnAddresses.push_back(ip);
        else
            numSkipCalls--;
    }
}

string StackTrace::toString()
{

}

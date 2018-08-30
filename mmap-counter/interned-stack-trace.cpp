#include "interned-stack-trace.h"

std::unordered_map<size_t, std::shared_ptr<StackTrace>> InternedStackTrace::s_stackTraces;

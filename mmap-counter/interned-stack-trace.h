#pragma once
#include <memory>
#include <unordered_map>
#include <cassert>
#include "stack-trace.h"

class InternedStackTrace
{
public:
    explicit InternedStackTrace() {
        StackTrace stackTrace;
        auto iter = s_stackTraces.find(stackTrace.hash());
        if (iter != s_stackTraces.end()) {
            m_stackTrace = iter->second;
        } else {
            m_stackTrace = std::make_shared<StackTrace>(std::move(stackTrace));
            assert(stackTrace.hash() == m_stackTrace->hash());
            s_stackTraces[m_stackTrace->hash()] = m_stackTrace;
        }
    }
    ~InternedStackTrace() {
        assert(!m_stackTrace || m_stackTrace.use_count() >= 2);
        if (m_stackTrace && m_stackTrace.use_count() == 2) {
            s_stackTraces.erase(m_stackTrace->hash());
            assert(m_stackTrace.use_count() == 1);
        }
    }

    const StackTrace& get() const {
        return *m_stackTrace;
    }

    const std::shared_ptr<StackTrace> getShared() const {
        return m_stackTrace;
    }

private:
    std::shared_ptr<StackTrace> m_stackTrace;

    static std::unordered_map<size_t, std::shared_ptr<StackTrace>> s_stackTraces;
};

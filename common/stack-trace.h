#pragma once
#include <vector>
#include <string>
#include <ostream>
#include <iostream>
using namespace std;

class StackTrace {
public:
    // Creates an stacktrace with the current stack. The topmost `numSkipCalls` are omitted (so that this
    StackTrace(int numSkipCalls = 0) noexcept;
    size_t hash() const { return m_hash; }

    bool operator==(const StackTrace& other) const noexcept;

private:
    friend ostream& operator<<(ostream& os, const StackTrace& st);
    friend struct std::hash<StackTrace>;

    vector<void*> m_returnAddresses; // top (recent) calls first
    size_t m_hash;
};

ostream& operator<<(ostream& os, const StackTrace& st);

namespace std {
    template <>
    struct hash<StackTrace> {
        size_t operator()(const StackTrace& st) const noexcept {
            return st.hash();
        }
    };
}

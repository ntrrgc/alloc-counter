#pragma once
#include <vector>
#include <string>
#include <ostream>
#include <iostream>
using namespace std;

class StackTrace {
public:
    // Creates an stacktrace with the current stack. The topmost `numSkipCalls` are omitted (so that this
    StackTrace(int numSkipCalls);

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
        size_t operator()(const StackTrace& st) noexcept {
            cerr << "hash function working, remove this comment!" << endl;
            return st.m_hash;
        }
    };
}

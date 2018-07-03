#pragma once
#include <vector>
#include <string>
using namespace std;

class StackTrace {
public:
    // Creates an stacktrace with the current stack. The topmost `numSkipCalls` are omitted (so that this
    StackTrace(int numSkipCalls);

    string toString();

private:
    vector<void*> m_returnAddresses;
};

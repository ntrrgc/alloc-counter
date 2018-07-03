#pragma once
#include <cstdint>

struct Environment {
    uint32_t timeForYoungToBecomeSuspicious = parseEnvironIntGreaterThanZero("ALLOC_TIME_SUSPICIOUS", 30);

private:
    static unsigned int parseEnvironIntGreaterThanZero(const char* name, int defaultValue);
};

extern Environment environment;

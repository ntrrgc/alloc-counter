#include "environment.h"
#include <unistd.h>
#include <cstdlib>

Environment environment;

unsigned int Environment::parseEnvironIntGreaterThanZero(const char *name, int defaultValue) {
    char* envString = getenv(name);
    if (!envString)
        return defaultValue;

    auto envInteger = atoi(envString);
    if (envInteger > 0)
        return envInteger;

    return defaultValue;
}

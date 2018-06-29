#include "patrol-thread.h"
#include "library-context.h"
#include "comm-memory.h"
#include "allocation-stats.h"
#include "allocation-table.h"
#include <unistd.h>
#include <cstdio>
#include <fstream>
#include <iostream>

PatrolThread* PatrolThread::s_instance = nullptr;

void PatrolThread::spawn() {
    s_instance = new PatrolThread;
}

uint32_t PatrolThread::LEAK_TIME()
{
    static bool initialized = false;
    static uint32_t value = 60;

    if (initialized)
        return value;

    initialized = true;
    char* envString = getenv("LEAK_TIME");
    if (!envString)
        return value;

    auto envInteger = atoi(envString);
    if (envInteger > 0)
        value = envInteger;
    return value;
}

void PatrolThread::monitorMain() {
    LibraryContext ctx;

    sleep(3);
    *__commMemory = WatchState::Watching;

    ofstream report("/tmp/alloc-report");
//    auto& report = cerr;
    report << "Patrol Thread Hello\n";
    report << "LEAK_TIME = " << LEAK_TIME() << " seconds\n";

    while (true) {
        sleep(5);
        if (getWatchState() == WatchState::Watching) {
            AllocationStats::instance().ensureEnabled();
            double t = AllocationStats::getTime() - AllocationStats::instance().timeWatchEnabled;
            // At least 1 second should pass before statistics are given, to avoid disproportionate values
            if (t <= 1)
                continue;

            report << "Allocs per second: " << AllocationStats::instance().allocationCount / t << endl;
            report << "Frees per second: " << AllocationStats::instance().freeCount / t << endl;
            report << "Reallocs per second: " << AllocationStats::instance().reallocCount / t << endl;

            {
                auto leakIterator = AllocationTable::instance().findLeakedEntries(LEAK_TIME());
                const AllocationInfo* alloc;
                while ((alloc = leakIterator->next())) {
                    report << alloc->memory << ": lost " << alloc->size << " bytes at " << alloc->callstackFingerprint << endl;
                }
            }

            report.flush();
        }
    }
}

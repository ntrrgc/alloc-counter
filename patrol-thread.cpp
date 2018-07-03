#include "patrol-thread.h"
#include "library-context.h"
#include "comm-memory.h"
#include "allocation-stats.h"
#include "allocation-table.h"
#include "environment.h"
#include <unistd.h>
#include <cstdio>
#include <fstream>
#include <iostream>

PatrolThread* PatrolThread::s_instance = nullptr;

void PatrolThread::spawn() {
    s_instance = new PatrolThread;
}

void PatrolThread::monitorMain() {
    LibraryContext ctx;

    sleep(3);
    *__commMemory = WatchState::Watching;

//    ofstream report("/tmp/alloc-report");
    auto& report = cerr;
    report << "Patrol Thread Hello\n";
    report << "ALLOC_TIME_SUSPICIOUS = " << environment.timeForAllocationToBecomeSuspicious << " seconds\n";

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
                auto leakIterator = AllocationTable::instance().findLeakedEntries(environment.timeForAllocationToBecomeSuspicious);
                const Allocation* alloc;
                while ((alloc = leakIterator->next())) {
                    report << alloc->memory << ": lost " << alloc->size << " bytes at " << alloc->callstackFingerprint << endl;
                }
            }

            report.flush();
        }
    }
}

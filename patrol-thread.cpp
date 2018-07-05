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
#include <tuple>

PatrolThread* PatrolThread::s_instance = nullptr;

void PatrolThread::spawn() {
    s_instance = new PatrolThread;
}

void PatrolThread::monitorMain() {
    LibraryContext ctx;

    sleep(15);
    *__commMemory = WatchState::Watching;

    ofstream report("/tmp/alloc-report");
//    auto& report = cerr;
    report << "Patrol Thread Hello\n";

    unordered_map<StackTrace, unsigned int> stackTraceToOccurrences;

    while (true) {
        sleep(5);
        AllocationStats stats;
        std::vector<AllocationTable::FoundLeak> leaks;
        std::tie(stats, leaks) = AllocationTable::instance().patrolThreadUpdateAllocationStates();
        double reportTime = AllocationStats::getTime();

        // At least 1 second should pass before statistics are given, to avoid disproportionate values
        if (stats.enabled && reportTime - stats.timeWatchEnabled >= 1.0) {
            double t = reportTime - stats.timeWatchEnabled;
            report << "Allocs per second: " << stats.allocationCount / t << endl;
            report << "Frees per second: " << stats.freeCount / t << endl;
            report << "Reallocs per second: " << stats.reallocCount / t << endl;
        }

        for (auto& leak : leaks) {
            auto& occurrencePair = *stackTraceToOccurrences.insert(make_pair(*leak.stackTrace, 0)).first;
            ++occurrencePair.second;

            if (occurrencePair.second == 1) {
                report << "[Callstack " << leak.stackTrace << "] Found new leak: lost "
                       << leak.memory << " (" << leak.size << " bytes)" << endl;
                report << *leak.stackTrace << endl;
            } else {
                report << "[Callstack " << leak.stackTrace << "] Lost "
                       << leak.memory << " (" << leak.size << " bytes), "
                       << occurrencePair.second << " times again." << endl;
            }
        }
        report.flush();
    }
}

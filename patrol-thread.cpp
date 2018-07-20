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
#include <string>
#include <sstream>
#include <cmath>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <inttypes.h>

PatrolThread* PatrolThread::s_instance = nullptr;

static string humanSize(double size) {
    static const array<const char*, 4> units {{"bytes", "kiB", "MiB", "GiB"}};
    const char* unit;
    for (auto i = units.begin(); i != units.end(); ++i) {
        unit = *i;
        if (size < 1024)
            break;
        size = size / 1024;
    }
    stringstream ss;
    ss << size << " " << unit;
    return ss.str();
}

void PatrolThread::spawn() {
    s_instance = new PatrolThread;
}

static bool startsWith(const char*__restrict aString, const char *__restrict prefix)
{
    while(*prefix) {
        if(*prefix++ != *aString++)
            return false;
    }
    return true;
}

static int64_t getRssKB() {
    FILE* fp = fopen("/proc/self/status", "r");
    if (fp < 0) {
        perror("open(/proc/self/status)");
        exit(1);
    }

    char* line = nullptr;
    size_t size = 0;
    int nread;
    while ((nread = getline(&line, &size, fp)) != -1) {
        if (startsWith(line, "VmRSS:")) {
            int64_t rssKB;
            int nelements = sscanf(line, "VmRSS: %" PRId64 " kB", &rssKB);
            if (nelements != 1) {
                perror("getRSS:scanf");
                exit(1);
            }
            free(line);
            return rssKB;
        }
    }
    throw std::runtime_error("Could not read VmRSS.");
}

void PatrolThread::monitorMain() {
    LibraryContext ctx;

    ofstream leakStream("/tmp/leak-report");
    ofstream progressStream("/tmp/alloc-report");
    ofstream memoryUsageStream("/tmp/memory-report");
    progressStream << "Patrol Thread Hello\n";

    if (environment.autoStartTime != 0) {
        sleep(environment.autoStartTime);
        *__commMemory = WatchState::Watching;
    }

    unordered_map<StackTrace, unsigned int> stackTraceToOccurrences;
    double timeNextLeakReport = 0;
    int64_t baseRSS = 0;
    time_t baseTime = 0;

    while (true) {
        sleep(5);
        AllocationStats stats;
        std::vector<AllocationTable::FoundLeak> leaks;
        std::tie(stats, leaks) = AllocationTable::instance().patrolThreadUpdateAllocationStates();
        double reportTime = AllocationStats::getTime();

        // At least 1 second should pass before statistics are given, to avoid disproportionate values
        if (stats.enabled && reportTime - stats.timeWatchEnabled >= 1.0) {
            double t = reportTime - stats.timeWatchEnabled;
            progressStream << "Allocs per second: " << stats.allocationCount / t << endl;
            progressStream << "Frees per second: " << stats.freeCount / t << endl;
            progressStream << "Reallocs per second: " << stats.reallocCount / t << endl;
        }

        if (stats.enabled && baseRSS == 0) {
            baseRSS = getRssKB();
            baseTime = time(nullptr);
            memoryUsageStream << "#Time\tRSS\tAllocated\n";
        }

        if (stats.enabled) {
            memoryUsageStream << time(nullptr) - baseTime << "\t"
                << 1024 * (getRssKB() - baseRSS) << "\t"
                << stats.liveBytes << endl;
        }

        for (auto& leak : leaks) {
            auto& occurrencePair = *stackTraceToOccurrences.insert(make_pair(*leak.stackTrace, 0)).first;
            ++occurrencePair.second;

            if (occurrencePair.second == 1) {
                progressStream << "[Callstack " << leak.stackTrace << "] Found new leak: lost "
                       << leak.memory << " (" << leak.size << " bytes)" << endl;
                progressStream << *leak.stackTrace << endl;
            } else {
                progressStream << "[Callstack " << leak.stackTrace << "] Lost "
                       << leak.memory << " (" << leak.size << " bytes), "
                       << occurrencePair.second << " times again." << endl;
            }
        }
        progressStream.flush();

        if (timeNextLeakReport == 0) {
            // Schedule the first leak report after the accounting has been running for some time.
            timeNextLeakReport = reportTime + environment.leakReportInterval;
        }

        if (reportTime > timeNextLeakReport) {
            AllocationTable::LeakReport leakReport = AllocationTable::instance().patrolThreadMakeLeakReport();
            leakStream << "[t=" << (reportTime - stats.timeWatchEnabled) << "] Begin leak report:"  << endl;
            if (!isnan(leakReport.ratioAllocationHasSuspiciousFingerprint)) {
                leakStream << "Ratio suspicious fingerprint/allocations: " <<
                             leakReport.ratioAllocationHasSuspiciousFingerprint << endl;
            }
            if (!isnan(leakReport.averageStackTracesPerFingerprint)) {
                leakStream << "Average number of stack traces per suspicious fingerprint: " <<
                              leakReport.averageStackTracesPerFingerprint << endl;
            }
            if (!isnan(leakReport.ratioLeakyStacks)) {
                leakStream << "Leaky stack traces ratio (non-leaky/maybe/leaky): " <<
                              leakReport.ratioNonLeakyStacks << " / " << leakReport.ratioMaybeLeakyStats <<
                              " / " << leakReport.ratioLeakyStacks << endl << endl;
            }

            for (AllocationTable::LeakReport::Leak& leak : leakReport.leaks) {
                leakStream << "[Callstack " << leak.stackTrace << "] lost ~" << humanSize(leak.lostBytesEstimated) <<
                              " in ~" << leak.lostAllocationsEstimated << " allocations (leak ratio = " << leak.leakRatio <<
                              ")" << endl;
                leakStream << *leak.stackTrace << endl;
            }
            leakStream << "End of leak report." << endl;
            leakStream.flush();

            // Schedule the next periodical leak report.
            timeNextLeakReport = reportTime + environment.leakReportInterval;
        }
    }
}

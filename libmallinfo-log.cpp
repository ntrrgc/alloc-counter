#include <thread>
#include <cstdlib>
#include <cstdio>
#include <cinttypes>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <malloc.h>
extern char *program_invocation_short_name;
using namespace std;

static double getTime() {
    timespec tv;
    clock_gettime(CLOCK_MONOTONIC, &tv);
    return tv.tv_sec + tv.tv_nsec / 1e9;
}

struct ProcessStatus {
    // These are values read from /proc/self/status
    uint64_t VmRSS = UINT64_MAX; // kiB
    uint64_t RssAnon = UINT64_MAX; // kiB
};

static ProcessStatus readProcessStatus() {
    ProcessStatus ret;
    FILE* fp = fopen("/proc/self/status", "r");
    if (!fp) {
        perror("open(/proc/self/status)");
        exit(1);
    }

    char* line = nullptr;
    size_t size = 0;
    int nread;
    while ((nread = getline(&line, &size, fp)) != -1) {
        sscanf(line, "VmRSS: %" PRIu64 " kB", &ret.VmRSS);
        sscanf(line, "RssAnon: %" PRIu64 " kB", &ret.RssAnon);
    }
    free(line);
    fclose(fp);

    if (ret.RssAnon == UINT64_MAX || ret.VmRSS == UINT64_MAX)
        throw std::runtime_error("Could not parse /proc/self/status.");
    return ret;
}

static string buildMemoryReportPath() {
    stringstream ss;
    ss << "/tmp/memory-" << program_invocation_short_name << "-" << getpid();
    return ss.str();
}

static void mallinfoThreadMain() {
    ofstream memoryUsageStream(buildMemoryReportPath());
    memoryUsageStream
        << "#Time\t"
        "Total RSS\t"
        "Total Anon RSS\t"
        "Arenas size\t"
        "Num free chunks\t"
        "Num mmap chunks\t"
        "mmaps size\t"
        "Used chunks size\t"
        "Free chunks size\t"
        "Top chunk size" << endl;

    while (true) {
        sleep(5);
        double time = getTime();
        struct mallinfo info = mallinfo();
        ProcessStatus processStatus = readProcessStatus();
        memoryUsageStream
            << time << "\t"
            << 1024 * processStatus.VmRSS << "\t"
            << 1024 * processStatus.RssAnon << "\t"
            << info.arena << "\t"
            << info.ordblks << "\t"
            << info.hblks << "\t"
            << info.hblkhd << "\t"
            << info.uordblks << "\t"
            << info.fordblks << "\t"
            << info.keepcost << endl;
    }
}

static thread* mallinfoThread = nullptr;

__attribute__((constructor)) void init(void) {
    mallinfoThread = new thread(mallinfoThreadMain);
}

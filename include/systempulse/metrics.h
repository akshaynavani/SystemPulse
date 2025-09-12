// Plain data types describing what SystemPulse reads out of /proc.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace systempulse {

// One line of /proc/stat ("cpu" or "cpuN"); values are in USER_HZ jiffies.
struct CpuTimes {
    std::string label;  // "cpu" for the aggregate, "cpu0", "cpu1", ... per core
    uint64_t user = 0;
    uint64_t nice = 0;
    uint64_t system = 0;
    uint64_t idle = 0;
    uint64_t iowait = 0;
    uint64_t irq = 0;
    uint64_t softirq = 0;
    uint64_t steal = 0;

    uint64_t idleTotal() const { return idle + iowait; }
    uint64_t busyTotal() const { return user + nice + system + irq + softirq + steal; }
    uint64_t total() const { return idleTotal() + busyTotal(); }
};

// Extra system-wide counters from /proc/stat.
struct StatCounters {
    uint64_t contextSwitches = 0;
    uint64_t processesCreated = 0;
    uint64_t procsRunning = 0;
    uint64_t procsBlocked = 0;
};

struct CpuSnapshot {
    std::vector<CpuTimes> cpus;  // cpus[0] is the aggregate line
    StatCounters counters;
};

// Selected fields from /proc/meminfo, all in kB.
struct MemInfo {
    uint64_t totalKb = 0;
    uint64_t freeKb = 0;
    uint64_t availableKb = 0;
    uint64_t buffersKb = 0;
    uint64_t cachedKb = 0;
    uint64_t swapTotalKb = 0;
    uint64_t swapFreeKb = 0;
    uint64_t dirtyKb = 0;

    uint64_t usedKb() const { return totalKb > availableKb ? totalKb - availableKb : 0; }
    double usedPercent() const {
        return totalKb == 0 ? 0.0 : 100.0 * static_cast<double>(usedKb()) / totalKb;
    }
    uint64_t swapUsedKb() const { return swapTotalKb > swapFreeKb ? swapTotalKb - swapFreeKb : 0; }
};

// One device line of /proc/diskstats.
struct DiskStats {
    std::string device;
    uint64_t readsCompleted = 0;
    uint64_t sectorsRead = 0;
    uint64_t readTimeMs = 0;
    uint64_t writesCompleted = 0;
    uint64_t sectorsWritten = 0;
    uint64_t writeTimeMs = 0;
    uint64_t ioInProgress = 0;
    uint64_t ioTimeMs = 0;
};

struct LoadAvg {
    double one = 0.0;
    double five = 0.0;
    double fifteen = 0.0;
    uint32_t runnable = 0;
    uint32_t total = 0;
};

// Selected fields from /proc/<pid>/stat.
struct ProcStat {
    int pid = 0;
    std::string comm;
    char state = '?';
    int ppid = 0;
    uint64_t minorFaults = 0;
    uint64_t majorFaults = 0;
    uint64_t utime = 0;   // jiffies
    uint64_t stime = 0;   // jiffies
    int64_t numThreads = 0;
    uint64_t startTime = 0;
    uint64_t vsizeBytes = 0;
    int64_t rssPages = 0;
    int processor = -1;

    uint64_t cpuJiffies() const { return utime + stime; }
};

// Selected fields from /proc/<pid>/status.
struct ProcStatus {
    uint64_t vmRssKb = 0;
    uint64_t vmHwmKb = 0;
    uint64_t voluntaryCtxSwitches = 0;
    uint64_t nonvoluntaryCtxSwitches = 0;
};

// /proc/<pid>/io (requires same uid or CAP_SYS_PTRACE).
struct ProcIo {
    uint64_t rchar = 0;
    uint64_t wchar = 0;
    uint64_t syscr = 0;
    uint64_t syscw = 0;
    uint64_t readBytes = 0;
    uint64_t writeBytes = 0;
    uint64_t cancelledWriteBytes = 0;
};

// Derived, rate-style metrics computed from two snapshots.
struct CpuUsage {
    std::string label;
    double userPct = 0.0;
    double systemPct = 0.0;
    double iowaitPct = 0.0;
    double stealPct = 0.0;
    double busyPct = 0.0;
};

struct DiskRates {
    std::string device;
    double readBytesPerSec = 0.0;
    double writeBytesPerSec = 0.0;
    double readIops = 0.0;
    double writeIops = 0.0;
    double utilizationPct = 0.0;  // fraction of wall time the device was busy
    double awaitMs = 0.0;         // average time per completed IO
};

// Delta helpers. `elapsedSec` must be > 0.
CpuUsage computeCpuUsage(const CpuTimes& prev, const CpuTimes& cur);
DiskRates computeDiskRates(const DiskStats& prev, const DiskStats& cur, double elapsedSec);
double computeProcessCpuPercent(uint64_t prevJiffies, uint64_t curJiffies, double elapsedSec,
                                long clockTicksPerSec);

// Counter difference that tolerates wraparound / resets by returning 0.
inline uint64_t counterDelta(uint64_t prev, uint64_t cur) { return cur >= prev ? cur - prev : 0; }

}  // namespace systempulse

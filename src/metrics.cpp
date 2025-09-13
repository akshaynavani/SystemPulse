#include "systempulse/metrics.h"

namespace systempulse {

namespace {
constexpr double kSectorBytes = 512.0;  // /proc/diskstats always reports 512-byte sectors

double pct(uint64_t part, uint64_t whole) {
    return whole == 0 ? 0.0 : 100.0 * static_cast<double>(part) / static_cast<double>(whole);
}
}  // namespace

CpuUsage computeCpuUsage(const CpuTimes& prev, const CpuTimes& cur) {
    CpuUsage u;
    u.label = cur.label;
    const uint64_t total = counterDelta(prev.total(), cur.total());
    if (total == 0) {
        return u;
    }
    u.userPct = pct(counterDelta(prev.user + prev.nice, cur.user + cur.nice), total);
    u.systemPct = pct(counterDelta(prev.system + prev.irq + prev.softirq,
                                   cur.system + cur.irq + cur.softirq),
                      total);
    u.iowaitPct = pct(counterDelta(prev.iowait, cur.iowait), total);
    u.stealPct = pct(counterDelta(prev.steal, cur.steal), total);
    u.busyPct = pct(counterDelta(prev.busyTotal(), cur.busyTotal()), total);
    return u;
}

DiskRates computeDiskRates(const DiskStats& prev, const DiskStats& cur, double elapsedSec) {
    DiskRates r;
    r.device = cur.device;
    if (elapsedSec <= 0.0) {
        return r;
    }
    const uint64_t reads = counterDelta(prev.readsCompleted, cur.readsCompleted);
    const uint64_t writes = counterDelta(prev.writesCompleted, cur.writesCompleted);
    r.readBytesPerSec = counterDelta(prev.sectorsRead, cur.sectorsRead) * kSectorBytes / elapsedSec;
    r.writeBytesPerSec =
        counterDelta(prev.sectorsWritten, cur.sectorsWritten) * kSectorBytes / elapsedSec;
    r.readIops = reads / elapsedSec;
    r.writeIops = writes / elapsedSec;

    const double busyMs = static_cast<double>(counterDelta(prev.ioTimeMs, cur.ioTimeMs));
    r.utilizationPct = 100.0 * busyMs / (elapsedSec * 1000.0);
    if (r.utilizationPct > 100.0) {
        r.utilizationPct = 100.0;
    }
    const uint64_t ios = reads + writes;
    if (ios > 0) {
        const uint64_t waitMs = counterDelta(prev.readTimeMs, cur.readTimeMs) +
                                counterDelta(prev.writeTimeMs, cur.writeTimeMs);
        r.awaitMs = static_cast<double>(waitMs) / static_cast<double>(ios);
    }
    return r;
}

double computeProcessCpuPercent(uint64_t prevJiffies, uint64_t curJiffies, double elapsedSec,
                                long clockTicksPerSec) {
    if (elapsedSec <= 0.0 || clockTicksPerSec <= 0) {
        return 0.0;
    }
    const double cpuSec =
        static_cast<double>(counterDelta(prevJiffies, curJiffies)) / clockTicksPerSec;
    // Can exceed 100% for multithreaded processes (like `top` in Irix mode).
    return 100.0 * cpuSec / elapsedSec;
}

}  // namespace systempulse

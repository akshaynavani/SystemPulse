#include "systempulse/collectors.h"

#include <algorithm>

namespace systempulse {

// ---------------------------------------------------------------- CpuCollector

CpuCollector::CpuCollector(int64_t intervalMs, SampleQueue& out, const ProcReader& reader,
                           bool perCore)
    : Collector("cpu", intervalMs, out), reader_(reader), perCore_(perCore) {}

void CpuCollector::collect(int64_t nowMs, double elapsedSec) {
    auto snap = reader_.cpu();
    if (!snap) {
        return;
    }
    if (prev_ && elapsedSec > 0.0) {
        const size_t n = std::min(prev_->cpus.size(), snap->cpus.size());
        for (size_t i = 0; i < n; ++i) {
            if (i > 0 && !perCore_) {
                break;
            }
            const auto u = computeCpuUsage(prev_->cpus[i], snap->cpus[i]);
            const std::string entity = i == 0 ? "all" : u.label;
            emit(nowMs, "cpu", entity, "busy_pct", u.busyPct);
            if (i == 0) {
                emit(nowMs, "cpu", entity, "user_pct", u.userPct);
                emit(nowMs, "cpu", entity, "system_pct", u.systemPct);
                emit(nowMs, "cpu", entity, "iowait_pct", u.iowaitPct);
                emit(nowMs, "cpu", entity, "steal_pct", u.stealPct);
            }
        }
        const auto& pc = prev_->counters;
        const auto& cc = snap->counters;
        emit(nowMs, "sys", "system", "ctx_switches_per_sec",
             counterDelta(pc.contextSwitches, cc.contextSwitches) / elapsedSec);
        emit(nowMs, "sys", "system", "forks_per_sec",
             counterDelta(pc.processesCreated, cc.processesCreated) / elapsedSec);
    }
    emit(nowMs, "sys", "system", "procs_running", static_cast<double>(snap->counters.procsRunning));
    emit(nowMs, "sys", "system", "procs_blocked", static_cast<double>(snap->counters.procsBlocked));
    prev_ = std::move(snap);
}

// ------------------------------------------------------------- MemoryCollector

MemoryCollector::MemoryCollector(int64_t intervalMs, SampleQueue& out, const ProcReader& reader)
    : Collector("mem", intervalMs, out), reader_(reader) {}

void MemoryCollector::collect(int64_t nowMs, double /*elapsedSec*/) {
    if (auto m = reader_.memory()) {
        emit(nowMs, "mem", "system", "total_kb", static_cast<double>(m->totalKb));
        emit(nowMs, "mem", "system", "used_kb", static_cast<double>(m->usedKb()));
        emit(nowMs, "mem", "system", "available_kb", static_cast<double>(m->availableKb));
        emit(nowMs, "mem", "system", "used_pct", m->usedPercent());
        emit(nowMs, "mem", "system", "cached_kb", static_cast<double>(m->cachedKb));
        emit(nowMs, "mem", "system", "dirty_kb", static_cast<double>(m->dirtyKb));
        emit(nowMs, "mem", "system", "swap_used_kb", static_cast<double>(m->swapUsedKb()));
    }
    if (auto l = reader_.loadAvg()) {
        emit(nowMs, "load", "system", "load1", l->one);
        emit(nowMs, "load", "system", "load5", l->five);
        emit(nowMs, "load", "system", "load15", l->fifteen);
        emit(nowMs, "load", "system", "threads_total", static_cast<double>(l->total));
    }
}

// ----------------------------------------------------------------- IoCollector

IoCollector::IoCollector(int64_t intervalMs, SampleQueue& out, const ProcReader& reader,
                         std::set<std::string> devices)
    : Collector("io", intervalMs, out), reader_(reader), devices_(std::move(devices)) {}

bool IoCollector::wanted(const std::string& device) const {
    return devices_.empty() ? isPhysicalDisk(device) : devices_.count(device) > 0;
}

void IoCollector::collect(int64_t nowMs, double elapsedSec) {
    auto disks = reader_.disks();
    if (!disks) {
        return;
    }
    std::map<std::string, DiskStats> current;
    for (const auto& d : *disks) {
        if (!wanted(d.device)) {
            continue;
        }
        current[d.device] = d;
        const auto it = prev_.find(d.device);
        if (it == prev_.end() || elapsedSec <= 0.0) {
            continue;
        }
        const auto r = computeDiskRates(it->second, d, elapsedSec);
        emit(nowMs, "io", d.device, "read_bps", r.readBytesPerSec);
        emit(nowMs, "io", d.device, "write_bps", r.writeBytesPerSec);
        emit(nowMs, "io", d.device, "read_iops", r.readIops);
        emit(nowMs, "io", d.device, "write_iops", r.writeIops);
        emit(nowMs, "io", d.device, "util_pct", r.utilizationPct);
        emit(nowMs, "io", d.device, "await_ms", r.awaitMs);
        emit(nowMs, "io", d.device, "queue_depth", static_cast<double>(d.ioInProgress));
    }
    prev_ = std::move(current);
}

}  // namespace systempulse

#include "systempulse/process_collector.h"

#include <dirent.h>
#include <unistd.h>

#include <cstdlib>

namespace systempulse {

namespace {
bool isNumeric(const char* s) {
    if (!*s) return false;
    for (; *s; ++s) {
        if (*s < '0' || *s > '9') return false;
    }
    return true;
}
}  // namespace

ProcessCollector::ProcessCollector(int64_t intervalMs, SampleQueue& out, const ProcReader& reader)
    : Collector("proc", intervalMs, out),
      reader_(reader),
      clockTicks_(sysconf(_SC_CLK_TCK)),
      pageSizeKb_(sysconf(_SC_PAGESIZE) / 1024) {
    if (clockTicks_ <= 0) clockTicks_ = 100;
    if (pageSizeKb_ <= 0) pageSizeKb_ = 4;
}

void ProcessCollector::track(int pid, std::string label) {
    LockGuard g(mu_);
    auto& t = tracked_[pid];
    if (!label.empty() || t.label.empty()) {
        t.label = label;
    }
}

void ProcessCollector::untrack(int pid) {
    LockGuard g(mu_);
    tracked_.erase(pid);
}

void ProcessCollector::trackByName(std::vector<std::string> names) {
    LockGuard g(mu_);
    names_ = std::move(names);
}

std::set<int> ProcessCollector::trackedPids() const {
    LockGuard g(mu_);
    std::set<int> out;
    for (const auto& kv : tracked_) out.insert(kv.first);
    return out;
}

std::vector<int> ProcessCollector::findPidsByName(const std::string& procRoot,
                                                  const std::vector<std::string>& names) {
    std::vector<int> out;
    if (names.empty()) return out;
    DIR* dir = opendir(procRoot.c_str());
    if (!dir) return out;
    ProcReader reader(procRoot);
    while (dirent* ent = readdir(dir)) {
        if (!isNumeric(ent->d_name)) continue;
        const int pid = std::atoi(ent->d_name);
        auto st = reader.pidStat(pid);
        if (!st) continue;
        for (const auto& n : names) {
            if (st->comm == n) {
                out.push_back(pid);
                break;
            }
        }
    }
    closedir(dir);
    return out;
}

void ProcessCollector::refreshNameMatches() {
    std::vector<std::string> names;
    {
        LockGuard g(mu_);
        names = names_;
    }
    if (names.empty()) return;
    const int self = static_cast<int>(getpid());
    for (int pid : findPidsByName(reader_.root(), names)) {
        if (pid == self) continue;
        LockGuard g(mu_);
        tracked_.emplace(pid, Tracked{});
    }
}

void ProcessCollector::collect(int64_t nowMs, double elapsedSec) {
    refreshNameMatches();

    std::map<int, Tracked> snapshot;
    {
        LockGuard g(mu_);
        snapshot = tracked_;
    }

    double totalCpu = 0.0;
    double totalRssKb = 0.0;
    double totalReadBps = 0.0;
    double totalWriteBps = 0.0;
    int alive = 0;
    std::vector<int> gone;

    for (auto& [pid, t] : snapshot) {
        auto st = reader_.pidStat(pid);
        // A zombie still has a /proc entry but no longer does any work.
        if (!st || st->state == 'Z' || st->state == 'X') {
            gone.push_back(pid);
            const std::string entity =
                "pid:" + std::to_string(pid) + (t.label.empty() ? "" : ":" + t.label);
            emit(nowMs, "proc", entity, "alive", 0.0);
            continue;
        }
        if (t.label.empty()) t.label = st->comm;
        const std::string entity = "pid:" + std::to_string(pid) + ":" + t.label;
        const auto status = reader_.pidStatus(pid);
        const auto io = reader_.pidIo(pid);  // may fail without permissions
        ++alive;

        const double rssKb = status && status->vmRssKb > 0
                                 ? static_cast<double>(status->vmRssKb)
                                 : static_cast<double>(st->rssPages * pageSizeKb_);
        emit(nowMs, "proc", entity, "alive", 1.0);
        emit(nowMs, "proc", entity, "rss_kb", rssKb);
        emit(nowMs, "proc", entity, "vsize_kb", static_cast<double>(st->vsizeBytes / 1024));
        emit(nowMs, "proc", entity, "threads", static_cast<double>(st->numThreads));
        emit(nowMs, "proc", entity, "last_cpu", static_cast<double>(st->processor));
        totalRssKb += rssKb;

        Previous& p = t.prev;
        if (p.valid && elapsedSec > 0.0) {
            const double cpu =
                computeProcessCpuPercent(p.cpuJiffies, st->cpuJiffies(), elapsedSec, clockTicks_);
            emit(nowMs, "proc", entity, "cpu_pct", cpu);
            emit(nowMs, "proc", entity, "minor_faults_per_sec",
                 counterDelta(p.minorFaults, st->minorFaults) / elapsedSec);
            emit(nowMs, "proc", entity, "major_faults_per_sec",
                 counterDelta(p.majorFaults, st->majorFaults) / elapsedSec);
            totalCpu += cpu;
            if (status) {
                emit(nowMs, "proc", entity, "vol_ctx_per_sec",
                     counterDelta(p.volCtx, status->voluntaryCtxSwitches) / elapsedSec);
                emit(nowMs, "proc", entity, "invol_ctx_per_sec",
                     counterDelta(p.involCtx, status->nonvoluntaryCtxSwitches) / elapsedSec);
            }
            if (io && p.hasIo) {
                const double rbps = counterDelta(p.readBytes, io->readBytes) / elapsedSec;
                const double wbps = counterDelta(p.writeBytes, io->writeBytes) / elapsedSec;
                emit(nowMs, "proc", entity, "disk_read_bps", rbps);
                emit(nowMs, "proc", entity, "disk_write_bps", wbps);
                emit(nowMs, "proc", entity, "logical_read_bps",
                     counterDelta(p.rchar, io->rchar) / elapsedSec);
                emit(nowMs, "proc", entity, "logical_write_bps",
                     counterDelta(p.wchar, io->wchar) / elapsedSec);
                totalReadBps += rbps;
                totalWriteBps += wbps;
            }
        }

        p.cpuJiffies = st->cpuJiffies();
        p.minorFaults = st->minorFaults;
        p.majorFaults = st->majorFaults;
        if (status) {
            p.volCtx = status->voluntaryCtxSwitches;
            p.involCtx = status->nonvoluntaryCtxSwitches;
        }
        if (io) {
            p.readBytes = io->readBytes;
            p.writeBytes = io->writeBytes;
            p.rchar = io->rchar;
            p.wchar = io->wchar;
        }
        p.hasIo = io.has_value();
        p.valid = true;
    }

    if (!snapshot.empty()) {
        emit(nowMs, "proc", "workers", "alive_count", alive);
        emit(nowMs, "proc", "workers", "rss_kb", totalRssKb);
        if (elapsedSec > 0.0) {
            emit(nowMs, "proc", "workers", "cpu_pct", totalCpu);
            emit(nowMs, "proc", "workers", "disk_read_bps", totalReadBps);
            emit(nowMs, "proc", "workers", "disk_write_bps", totalWriteBps);
        }
    }

    // Write back updated baselines, without clobbering concurrent track()/untrack().
    LockGuard g(mu_);
    for (auto& [pid, t] : snapshot) {
        auto it = tracked_.find(pid);
        if (it != tracked_.end()) it->second = t;
    }
    for (int pid : gone) tracked_.erase(pid);
}

}  // namespace systempulse

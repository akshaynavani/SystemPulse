// systempulse: multithreaded Linux resource monitor.

#include <unistd.h>

#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "systempulse/collectors.h"
#include "systempulse/monitor.h"
#include "systempulse/options.h"
#include "systempulse/proc_parser.h"
#include "systempulse/process_collector.h"
#include "systempulse/worker_pool.h"

#ifndef SYSTEMPULSE_VERSION
#define SYSTEMPULSE_VERSION "0.1.0"
#endif

using namespace systempulse;

namespace {

double lookupLatest(const std::map<SeriesKey, double>& m, const char* source, const char* entity,
                    const char* metric, double fallback = -1.0) {
    const auto it = m.find(SeriesKey{source, entity, metric});
    return it == m.end() ? fallback : it->second;
}

std::string humanBytes(double bps) {
    const char* units[] = {"B", "KB", "MB", "GB"};
    int u = 0;
    while (bps >= 1024.0 && u < 3) {
        bps /= 1024.0;
        ++u;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f%s", bps, units[u]);
    return buf;
}

// One-line live status written to stderr so it never mixes with CSV on stdout.
void printLiveLine(const CsvWriter& writer, bool tty) {
    const auto latest = writer.latest();
    const double cpu = lookupLatest(latest, "cpu", "all", "busy_pct");
    const double mem = lookupLatest(latest, "mem", "system", "used_pct");
    const double load1 = lookupLatest(latest, "load", "system", "load1");

    double rd = 0, wr = 0, util = 0;
    for (const auto& [k, v] : latest) {
        if (k.source != "io") continue;
        if (k.metric == "read_bps") rd += v;
        if (k.metric == "write_bps") wr += v;
        if (k.metric == "util_pct" && v > util) util = v;
    }
    const double wCpu = lookupLatest(latest, "proc", "workers", "cpu_pct");
    const double wRss = lookupLatest(latest, "proc", "workers", "rss_kb");
    const double wAlive = lookupLatest(latest, "proc", "workers", "alive_count");

    char line[256];
    int n = std::snprintf(line, sizeof(line),
                          "cpu %5.1f%% | mem %5.1f%% | load %5.2f | io r %s/s w %s/s max util %4.1f%%",
                          cpu < 0 ? 0 : cpu, mem < 0 ? 0 : mem, load1 < 0 ? 0 : load1,
                          humanBytes(rd).c_str(), humanBytes(wr).c_str(), util);
    if (wAlive >= 0 && n > 0 && n < static_cast<int>(sizeof(line))) {
        std::snprintf(line + n, sizeof(line) - n, " | workers %d cpu %.1f%% rss %s",
                      static_cast<int>(wAlive), wCpu < 0 ? 0 : wCpu,
                      humanBytes((wRss < 0 ? 0 : wRss) * 1024).c_str());
    }
    if (tty) {
        std::fprintf(stderr, "\r\033[K%s", line);
    } else {
        std::fprintf(stderr, "%s\n", line);
    }
    std::fflush(stderr);
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv, argv + argc);
    const ParseResult parsed = parseOptions(args);
    if (!parsed.ok) {
        std::fprintf(stderr, "systempulse: %s\n\n%s", parsed.error.c_str(),
                     usageText(args[0]).c_str());
        return 2;
    }
    const Options& opt = parsed.options;
    if (opt.showHelp) {
        std::printf("%s", usageText(args[0]).c_str());
        return 0;
    }
    if (opt.showVersion) {
        std::printf("systempulse %s\n", SYSTEMPULSE_VERSION);
        return 0;
    }

    ProcReader reader(opt.procRoot);
    if (!reader.cpu()) {
        std::fprintf(stderr, "systempulse: cannot read %s/stat — is this Linux?\n",
                     opt.procRoot.c_str());
        return 1;
    }

    // Spawn workers first, while the process is still single-threaded, so fork()
    // never copies a half-held mutex from another thread.
    std::unique_ptr<WorkerPool> pool;
    if (opt.spawnCount > 0) {
        pool = std::make_unique<WorkerPool>(WorkerPool::splitCommand(opt.workerCmd));
        if (!pool->spawn(opt.spawnCount)) {
            return 1;
        }
        std::fprintf(stderr, "systempulse: spawned %d workers: %s\n", opt.spawnCount,
                     opt.workerCmd.c_str());
    }

    Monitor monitor(opt.output, opt.queueCapacity, opt.append);
    auto& q = monitor.queue();
    monitor.addCollector(
        std::make_unique<CpuCollector>(opt.effective(opt.cpuIntervalMs), q, reader, opt.perCore));
    monitor.addCollector(
        std::make_unique<MemoryCollector>(opt.effective(opt.memIntervalMs), q, reader));
    monitor.addCollector(
        std::make_unique<IoCollector>(opt.effective(opt.ioIntervalMs), q, reader, opt.devices));

    const bool trackProcesses = !opt.pids.empty() || !opt.matchNames.empty() || pool;
    if (trackProcesses) {
        auto proc = std::make_unique<ProcessCollector>(opt.effective(opt.procIntervalMs), q, reader);
        for (int pid : opt.pids) proc->track(pid);
        if (pool) {
            for (const auto& w : pool->workers()) proc->track(w.pid, w.label);
        }
        proc->trackByName(opt.matchNames);
        monitor.addCollector(std::move(proc));
    }

    const bool live = !opt.quiet;
    const bool tty = isatty(STDERR_FILENO);
    if (live) {
        monitor.setTicker(1000, [tty](const CsvWriter& w) { printLiveLine(w, tty); });
    }
    if (pool) {
        WorkerPool* p = pool.get();
        const bool exitWith = opt.exitWithWorkers;
        monitor.setWatchdog([p, exitWith] { return !(p->reap() == 0 && exitWith); });
    }

    std::fprintf(stderr, "systempulse: sampling every %lld ms -> %s%s\n",
                 static_cast<long long>(opt.intervalMs), opt.output.c_str(),
                 opt.durationSec > 0 ? "" : " (Ctrl-C to stop)");

    const MonitorStats stats = monitor.run(opt.durationSec);
    if (live && tty) std::fprintf(stderr, "\n");

    if (pool) {
        pool->terminateAll();
        for (const auto& w : pool->workers()) {
            std::fprintf(stderr, "  %s (pid %d): %s\n", w.label.c_str(), static_cast<int>(w.pid),
                         WorkerPool::describeStatus(w.exitStatus).c_str());
        }
    }

    std::fprintf(stderr, "systempulse: %.1fs, %llu rows written, %llu samples dropped\n",
                 stats.wallSeconds, static_cast<unsigned long long>(stats.rowsWritten),
                 static_cast<unsigned long long>(stats.samplesDropped));
    for (const auto& c : stats.collectors) {
        const double overheadPct = stats.wallSeconds > 0 ? 100.0 * c.busySeconds / stats.wallSeconds : 0;
        std::fprintf(stderr, "  collector %-5s ticks=%-6llu busy=%.3fs (%.3f%% of wall)\n",
                     c.name.c_str(), static_cast<unsigned long long>(c.ticks), c.busySeconds,
                     overheadPct);
    }
    return 0;
}

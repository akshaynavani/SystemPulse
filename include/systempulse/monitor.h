// Wires collectors, the sample queue, the CSV writer and signal handling together.
#pragma once

#include <atomic>
#include <csignal>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "systempulse/collector.h"
#include "systempulse/csv_writer.h"
#include "systempulse/thread.h"

namespace systempulse {

struct MonitorStats {
    double wallSeconds = 0.0;
    uint64_t rowsWritten = 0;
    uint64_t samplesDropped = 0;
    int stopSignal = 0;  // signal that ended the run, 0 if duration elapsed
    struct CollectorStat {
        std::string name;
        uint64_t ticks;
        double busySeconds;
    };
    std::vector<CollectorStat> collectors;
};

class Monitor {
public:
    Monitor(std::string outputPath, size_t queueCapacity = 65536, bool append = false);
    ~Monitor();

    SampleQueue& queue() { return queue_; }

    void addCollector(std::unique_ptr<Collector> c);

    // Called on the main thread every `everyMs` while running (e.g. live console view).
    void setTicker(int64_t everyMs, std::function<void(const CsvWriter&)> fn);
    // Called on the main thread every tick before the ticker; return false to stop.
    void setWatchdog(std::function<bool()> fn) { watchdog_ = std::move(fn); }

    // Runs until `durationSec` elapses (<= 0 means forever), SIGINT/SIGTERM
    // arrives, or the watchdog returns false. Must be called before any other
    // threads are created by the program so the signal mask is inherited.
    MonitorStats run(double durationSec);

    // Request an early stop from any thread.
    void requestStop() { stop_.request(); }

private:
    void signalLoop();

    SampleQueue queue_;
    CsvWriter writer_;
    std::vector<std::unique_ptr<Collector>> collectors_;
    StopSignal stop_;
    PThread signalThread_;
    sigset_t signalSet_{};
    std::atomic<int> caughtSignal_{0};

    int64_t tickerMs_ = 0;
    std::function<void(const CsvWriter&)> ticker_;
    std::function<bool()> watchdog_;
};

}  // namespace systempulse

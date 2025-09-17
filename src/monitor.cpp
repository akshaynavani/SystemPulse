#include "systempulse/monitor.h"

#include <pthread.h>
#include <signal.h>

#include <cstdio>

namespace systempulse {

Monitor::Monitor(std::string outputPath, size_t queueCapacity, bool append)
    : queue_(queueCapacity, SampleQueue::OverflowPolicy::DropOldest),
      writer_(queue_, std::move(outputPath), append) {
    sigemptyset(&signalSet_);
    sigaddset(&signalSet_, SIGINT);
    sigaddset(&signalSet_, SIGTERM);
    sigaddset(&signalSet_, SIGUSR1);  // internal wakeup used to end signalLoop()
}

Monitor::~Monitor() {
    stop_.request();
    for (auto& c : collectors_) {
        c->stop();
    }
    queue_.close();
}

void Monitor::addCollector(std::unique_ptr<Collector> c) { collectors_.push_back(std::move(c)); }

void Monitor::setTicker(int64_t everyMs, std::function<void(const CsvWriter&)> fn) {
    tickerMs_ = everyMs;
    ticker_ = std::move(fn);
}

void Monitor::signalLoop() {
    // Keep consuming signals until the main thread sends the SIGUSR1 shutdown
    // wakeup. Exiting only on SIGUSR1 guarantees this thread is still alive when
    // run() targets it with pthread_kill().
    for (;;) {
        int sig = 0;
        if (sigwait(&signalSet_, &sig) != 0) {
            continue;
        }
        if (sig == SIGUSR1) {
            break;
        }
        if (caughtSignal_ == 0) {
            caughtSignal_ = sig;
            std::fprintf(stderr, "\nsystempulse: caught signal %d, shutting down...\n", sig);
        }
        stop_.request();
    }
}

MonitorStats Monitor::run(double durationSec) {
    MonitorStats stats;

    // Block the signals in this thread; every pthread created below inherits the
    // mask, so only signalThread_ (via sigwait) ever observes them.
    sigset_t oldMask;
    pthread_sigmask(SIG_BLOCK, &signalSet_, &oldMask);

    if (!writer_.start()) {
        pthread_sigmask(SIG_SETMASK, &oldMask, nullptr);
        return stats;
    }
    signalThread_.start("sp-signals", [this] { signalLoop(); });
    for (auto& c : collectors_) {
        if (!c->start()) {
            std::fprintf(stderr, "systempulse: failed to start collector %s\n", c->name().c_str());
        }
    }

    const double t0 = monotonicSeconds();
    const int64_t sliceMs = tickerMs_ > 0 ? tickerMs_ : 250;
    while (!stop_.requested()) {
        int64_t waitMs = sliceMs;
        if (durationSec > 0) {
            const double remaining = durationSec - (monotonicSeconds() - t0);
            if (remaining <= 0) {
                break;
            }
            if (remaining * 1000.0 < static_cast<double>(waitMs)) {
                waitMs = static_cast<int64_t>(remaining * 1000.0) + 1;
            }
        }
        if (stop_.waitFor(waitMs)) {
            break;
        }
        if (watchdog_ && !watchdog_()) {
            break;
        }
        if (ticker_) {
            ticker_(writer_);
        }
    }
    stop_.request();

    // Thread-directed wakeup so the signal thread leaves sigwait() and exits.
    if (signalThread_.running()) {
        pthread_kill(signalThread_.native(), SIGUSR1);
        signalThread_.join();
    }

    for (auto& c : collectors_) {
        c->stop();
    }
    for (auto& c : collectors_) {
        c->join();
    }
    queue_.close();
    writer_.join();

    pthread_sigmask(SIG_SETMASK, &oldMask, nullptr);

    stats.wallSeconds = monotonicSeconds() - t0;
    stats.rowsWritten = writer_.rowsWritten();
    stats.samplesDropped = queue_.dropped();
    stats.stopSignal = caughtSignal_;
    for (const auto& c : collectors_) {
        stats.collectors.push_back({c->name(), c->ticks(), c->busySeconds()});
    }
    return stats;
}

}  // namespace systempulse

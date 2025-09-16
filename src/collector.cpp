#include "systempulse/collector.h"

namespace systempulse {

Collector::Collector(std::string name, int64_t intervalMs, SampleQueue& out)
    : name_(std::move(name)), intervalMs_(intervalMs > 0 ? intervalMs : 1000), out_(out) {}

Collector::~Collector() {
    stop();
    join();
}

bool Collector::start() {
    return thread_.start("sp-" + name_, [this] { run(); });
}

void Collector::stop() { stop_.request(); }

void Collector::join() { thread_.join(); }

void Collector::emit(int64_t ts, const std::string& source, const std::string& entity,
                     const std::string& metric, double value) {
    out_.push(Sample(ts, source, entity, metric, value));
}

void Collector::run() {
    // Schedule against absolute deadlines so collection time doesn't cause drift.
    timespec next = CondVar::now();
    double prevMono = 0.0;
    while (!stop_.requested()) {
        const double mono = monotonicSeconds();
        const double elapsed = prevMono == 0.0 ? 0.0 : mono - prevMono;
        prevMono = mono;

        collect(wallClockMillis(), elapsed);
        const double spent = monotonicSeconds() - mono;
        busyMicros_.fetch_add(static_cast<uint64_t>(spent * 1e6));
        ticks_.fetch_add(1);

        next = CondVar::addMillis(next, intervalMs_);
        const timespec now = CondVar::now();
        if (now.tv_sec > next.tv_sec || (now.tv_sec == next.tv_sec && now.tv_nsec > next.tv_nsec)) {
            next = now;  // we overran an interval; resync instead of bursting
        }
        if (stop_.waitUntil(next)) {
            break;
        }
    }
}

}  // namespace systempulse

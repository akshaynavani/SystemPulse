// Base class for a periodic sampler running on its own pthread.
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "systempulse/queue.h"
#include "systempulse/sample.h"
#include "systempulse/thread.h"

namespace systempulse {

using SampleQueue = BoundedQueue<Sample>;

class Collector {
public:
    Collector(std::string name, int64_t intervalMs, SampleQueue& out);
    virtual ~Collector();

    bool start();
    void stop();
    void join();

    const std::string& name() const { return name_; }
    int64_t intervalMs() const { return intervalMs_; }
    uint64_t ticks() const { return ticks_.load(); }
    // Total time spent inside collect(), useful for measuring monitor overhead.
    double busySeconds() const { return busyMicros_.load() / 1e6; }

protected:
    // Called once per interval. `elapsedSec` is the monotonic time since the
    // previous call (0 on the first call, where only a baseline should be taken).
    virtual void collect(int64_t nowMs, double elapsedSec) = 0;

    void emit(int64_t ts, const std::string& source, const std::string& entity,
              const std::string& metric, double value);

private:
    void run();

    std::string name_;
    int64_t intervalMs_;
    SampleQueue& out_;
    PThread thread_;
    StopSignal stop_;
    std::atomic<uint64_t> ticks_{0};
    std::atomic<uint64_t> busyMicros_{0};
};

}  // namespace systempulse

#pragma once

#include <cstdint>
#include <string>
#include <utility>

namespace systempulse {

// One measurement in long format; this is exactly one CSV row.
struct Sample {
    int64_t timestampMs = 0;  // wall-clock epoch milliseconds
    std::string source;       // "cpu", "mem", "io", "load", "proc", "sys"
    std::string entity;       // "cpu0", "sda", "pid:1234:worker", "system", ...
    std::string metric;       // "busy_pct", "read_bps", ...
    double value = 0.0;

    Sample() = default;
    Sample(int64_t ts, std::string src, std::string ent, std::string met, double v)
        : timestampMs(ts),
          source(std::move(src)),
          entity(std::move(ent)),
          metric(std::move(met)),
          value(v) {}
};

}  // namespace systempulse

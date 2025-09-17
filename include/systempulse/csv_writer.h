// Consumer thread that drains the sample queue into a long-format CSV file and
// keeps a snapshot of the most recent value per series for live display.
#pragma once

#include <cstdio>
#include <map>
#include <string>
#include <tuple>

#include "systempulse/collector.h"
#include "systempulse/thread.h"

namespace systempulse {

struct SeriesKey {
    std::string source;
    std::string entity;
    std::string metric;
    bool operator<(const SeriesKey& o) const {
        return std::tie(source, entity, metric) < std::tie(o.source, o.entity, o.metric);
    }
};

class CsvWriter {
public:
    // `path` of "-" writes to stdout.
    CsvWriter(SampleQueue& in, std::string path, bool append = false);
    ~CsvWriter();

    bool open();
    bool start();
    // Returns after the queue has been closed and fully drained.
    void join();

    uint64_t rowsWritten() const;
    std::map<SeriesKey, double> latest() const;

    static std::string escapeField(const std::string& field);
    static std::string formatRow(const Sample& s);
    static const char* header() { return "timestamp_ms,source,entity,metric,value"; }

private:
    void run();

    SampleQueue& in_;
    std::string path_;
    bool append_;
    FILE* fp_ = nullptr;
    bool ownsFile_ = false;
    PThread thread_;

    mutable Mutex mu_;
    std::map<SeriesKey, double> latest_;
    uint64_t rows_ = 0;
};

}  // namespace systempulse

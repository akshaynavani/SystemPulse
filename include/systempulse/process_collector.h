// Per-process collector for a dynamic set of worker PIDs.
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "systempulse/collector.h"
#include "systempulse/proc_parser.h"

namespace systempulse {

class ProcessCollector : public Collector {
public:
    ProcessCollector(int64_t intervalMs, SampleQueue& out, const ProcReader& reader);

    // Thread-safe; may be called while the collector is running.
    void track(int pid, std::string label = "");
    void untrack(int pid);
    // Processes whose comm equals one of these names are picked up on every tick.
    void trackByName(std::vector<std::string> names);

    std::set<int> trackedPids() const;

    // Scans procRoot for numeric directories whose comm matches any of `names`.
    static std::vector<int> findPidsByName(const std::string& procRoot,
                                           const std::vector<std::string>& names);

protected:
    void collect(int64_t nowMs, double elapsedSec) override;

private:
    struct Previous {
        uint64_t cpuJiffies = 0;
        uint64_t readBytes = 0;
        uint64_t writeBytes = 0;
        uint64_t rchar = 0;
        uint64_t wchar = 0;
        uint64_t minorFaults = 0;
        uint64_t majorFaults = 0;
        uint64_t volCtx = 0;
        uint64_t involCtx = 0;
        bool hasIo = false;
        bool valid = false;
    };
    struct Tracked {
        std::string label;
        Previous prev;
    };

    void refreshNameMatches();

    const ProcReader& reader_;
    long clockTicks_;
    long pageSizeKb_;

    mutable Mutex mu_;  // guards tracked_ and names_
    std::map<int, Tracked> tracked_;
    std::vector<std::string> names_;
};

}  // namespace systempulse

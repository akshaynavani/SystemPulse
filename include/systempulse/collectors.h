// System-wide collectors: CPU, memory/load, block IO.
#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>

#include "systempulse/collector.h"
#include "systempulse/proc_parser.h"

namespace systempulse {

class CpuCollector : public Collector {
public:
    CpuCollector(int64_t intervalMs, SampleQueue& out, const ProcReader& reader,
                 bool perCore = true);

protected:
    void collect(int64_t nowMs, double elapsedSec) override;

private:
    const ProcReader& reader_;
    bool perCore_;
    std::optional<CpuSnapshot> prev_;
};

class MemoryCollector : public Collector {
public:
    MemoryCollector(int64_t intervalMs, SampleQueue& out, const ProcReader& reader);

protected:
    void collect(int64_t nowMs, double elapsedSec) override;

private:
    const ProcReader& reader_;
};

class IoCollector : public Collector {
public:
    // Empty `devices` means "all physical disks" (see isPhysicalDisk()).
    IoCollector(int64_t intervalMs, SampleQueue& out, const ProcReader& reader,
                std::set<std::string> devices = {});

protected:
    void collect(int64_t nowMs, double elapsedSec) override;

private:
    bool wanted(const std::string& device) const;

    const ProcReader& reader_;
    std::set<std::string> devices_;
    std::map<std::string, DiskStats> prev_;
};

}  // namespace systempulse

// Parsers for /proc files. Each parseX() takes the raw file contents so it can be
// unit-tested with fixtures; readX() helpers do the actual file IO.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "systempulse/metrics.h"

namespace systempulse {

// Reads an entire (pseudo-)file. /proc files report size 0, so this reads until EOF
// using POSIX open/read. Returns std::nullopt if the file can't be opened.
std::optional<std::string> readFile(const std::string& path);

CpuSnapshot parseProcStat(const std::string& content);
MemInfo parseMemInfo(const std::string& content);
std::vector<DiskStats> parseDiskStats(const std::string& content);
std::optional<LoadAvg> parseLoadAvg(const std::string& content);
std::optional<ProcStat> parsePidStat(const std::string& content);
ProcStatus parsePidStatus(const std::string& content);
ProcIo parsePidIo(const std::string& content);

// True for whole-disk style devices worth monitoring by default (sda, nvme0n1, vda, xvda, ...)
// and false for partitions, loop and ram devices.
bool isPhysicalDisk(const std::string& device);

class ProcReader {
public:
    explicit ProcReader(std::string procRoot = "/proc");

    std::optional<CpuSnapshot> cpu() const;
    std::optional<MemInfo> memory() const;
    std::optional<std::vector<DiskStats>> disks() const;
    std::optional<LoadAvg> loadAvg() const;
    std::optional<ProcStat> pidStat(int pid) const;
    std::optional<ProcStatus> pidStatus(int pid) const;
    std::optional<ProcIo> pidIo(int pid) const;
    bool pidExists(int pid) const;

    const std::string& root() const { return root_; }

private:
    std::string pidPath(int pid, const char* file) const;
    std::string root_;
};

}  // namespace systempulse

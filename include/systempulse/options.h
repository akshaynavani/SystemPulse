// Command-line options for the systempulse binary.
#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace systempulse {

struct Options {
    int64_t intervalMs = 1000;
    int64_t cpuIntervalMs = 0;   // 0 = use intervalMs
    int64_t memIntervalMs = 0;
    int64_t ioIntervalMs = 0;
    int64_t procIntervalMs = 0;
    double durationSec = 0;      // 0 = until SIGINT/SIGTERM
    std::string output = "systempulse.csv";
    bool append = false;
    bool perCore = true;
    bool quiet = false;
    std::string procRoot = "/proc";
    std::set<std::string> devices;
    std::vector<int> pids;
    std::vector<std::string> matchNames;
    int spawnCount = 0;
    std::string workerCmd = "./pulse_worker --mode mixed";
    bool exitWithWorkers = false;
    size_t queueCapacity = 65536;

    bool showHelp = false;
    bool showVersion = false;

    int64_t effective(int64_t specific) const { return specific > 0 ? specific : intervalMs; }
};

struct ParseResult {
    bool ok = true;
    std::string error;
    Options options;
};

ParseResult parseOptions(const std::vector<std::string>& args);  // args[0] is program name
std::string usageText(const std::string& prog);

}  // namespace systempulse

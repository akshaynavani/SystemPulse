#include "systempulse/options.h"

#include <getopt.h>

#include <cerrno>
#include <cstdlib>
#include <sstream>

namespace systempulse {

namespace {

enum LongOnly {
    kCpuInterval = 1000,
    kMemInterval,
    kIoInterval,
    kProcInterval,
    kAppend,
    kNoPerCore,
    kDevices,
    kProcRoot,
    kExitWithWorkers,
    kQueueCapacity,
};

bool parseInt(const char* s, int64_t& out) {
    errno = 0;
    char* end = nullptr;
    const long long v = std::strtoll(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') return false;
    out = v;
    return true;
}

bool parseDouble(const char* s, double& out) {
    errno = 0;
    char* end = nullptr;
    out = std::strtod(s, &end);
    return errno == 0 && end != s && *end == '\0';
}

std::vector<std::string> splitCsv(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

void resetGetopt() {
#if defined(__GLIBC__)
    optind = 0;  // full reinitialisation on glibc
#else
    optind = 1;
#if defined(__APPLE__) || defined(__FreeBSD__)
    optreset = 1;
#endif
#endif
    opterr = 0;
}

}  // namespace

std::string usageText(const std::string& prog) {
    std::ostringstream os;
    os << "usage: " << prog << " [options]\n"
       << "\nSampling:\n"
       << "  -i, --interval MS         default sampling interval (default 1000)\n"
       << "      --cpu-interval MS     override interval for the CPU collector\n"
       << "      --mem-interval MS     override interval for the memory collector\n"
       << "      --io-interval MS      override interval for the IO collector\n"
       << "      --proc-interval MS    override interval for the process collector\n"
       << "  -d, --duration SEC        stop after SEC seconds (default: until Ctrl-C)\n"
       << "      --no-per-core         only report aggregate CPU usage\n"
       << "      --devices LIST        comma-separated block devices (default: all disks)\n"
       << "\nProcesses:\n"
       << "  -p, --pid PID[,PID...]    track existing processes (repeatable)\n"
       << "  -m, --match NAME          track processes whose comm equals NAME (repeatable)\n"
       << "  -s, --spawn N             spawn N worker processes and track them\n"
       << "  -w, --worker-cmd CMD      command used with --spawn\n"
       << "                            (default: \"./pulse_worker --mode mixed\")\n"
       << "      --exit-with-workers   stop monitoring once all spawned workers exit\n"
       << "\nOutput:\n"
       << "  -o, --output PATH         CSV output path, '-' for stdout (default systempulse.csv)\n"
       << "      --append              append to an existing CSV instead of truncating\n"
       << "  -q, --quiet               no live console summary\n"
       << "      --queue-capacity N    max buffered samples before dropping oldest\n"
       << "      --proc-root DIR       read from DIR instead of /proc\n"
       << "\n  -h, --help                show this help\n"
       << "  -V, --version             print version\n";
    return os.str();
}

ParseResult parseOptions(const std::vector<std::string>& args) {
    ParseResult r;
    Options& o = r.options;

    std::vector<std::string> storage = args;
    if (storage.empty()) storage.push_back("systempulse");
    std::vector<char*> argv;
    for (auto& s : storage) argv.push_back(&s[0]);
    argv.push_back(nullptr);
    const int argc = static_cast<int>(storage.size());

    static const option longOpts[] = {
        {"interval", required_argument, nullptr, 'i'},
        {"cpu-interval", required_argument, nullptr, kCpuInterval},
        {"mem-interval", required_argument, nullptr, kMemInterval},
        {"io-interval", required_argument, nullptr, kIoInterval},
        {"proc-interval", required_argument, nullptr, kProcInterval},
        {"duration", required_argument, nullptr, 'd'},
        {"output", required_argument, nullptr, 'o'},
        {"append", no_argument, nullptr, kAppend},
        {"no-per-core", no_argument, nullptr, kNoPerCore},
        {"devices", required_argument, nullptr, kDevices},
        {"pid", required_argument, nullptr, 'p'},
        {"match", required_argument, nullptr, 'm'},
        {"spawn", required_argument, nullptr, 's'},
        {"worker-cmd", required_argument, nullptr, 'w'},
        {"exit-with-workers", no_argument, nullptr, kExitWithWorkers},
        {"queue-capacity", required_argument, nullptr, kQueueCapacity},
        {"proc-root", required_argument, nullptr, kProcRoot},
        {"quiet", no_argument, nullptr, 'q'},
        {"help", no_argument, nullptr, 'h'},
        {"version", no_argument, nullptr, 'V'},
        {nullptr, 0, nullptr, 0},
    };

    auto fail = [&r](const std::string& msg) {
        r.ok = false;
        r.error = msg;
        return r;
    };

    resetGetopt();
    int c;
    while ((c = getopt_long(argc, argv.data(), ":i:d:o:p:m:s:w:qhV", longOpts, nullptr)) != -1) {
        int64_t n = 0;
        switch (c) {
            case 'i':
                if (!parseInt(optarg, n) || n < 10) return fail("--interval must be >= 10 ms");
                o.intervalMs = n;
                break;
            case kCpuInterval:
            case kMemInterval:
            case kIoInterval:
            case kProcInterval: {
                if (!parseInt(optarg, n) || n < 10) return fail("collector intervals must be >= 10 ms");
                int64_t* target = c == kCpuInterval   ? &o.cpuIntervalMs
                                  : c == kMemInterval ? &o.memIntervalMs
                                  : c == kIoInterval  ? &o.ioIntervalMs
                                                      : &o.procIntervalMs;
                *target = n;
                break;
            }
            case 'd':
                if (!parseDouble(optarg, o.durationSec) || o.durationSec < 0)
                    return fail("--duration must be a non-negative number of seconds");
                break;
            case 'o':
                o.output = optarg;
                break;
            case kAppend:
                o.append = true;
                break;
            case kNoPerCore:
                o.perCore = false;
                break;
            case kDevices:
                for (auto& d : splitCsv(optarg)) o.devices.insert(d);
                break;
            case 'p':
                for (auto& p : splitCsv(optarg)) {
                    if (!parseInt(p.c_str(), n) || n <= 0) return fail("invalid pid: " + p);
                    o.pids.push_back(static_cast<int>(n));
                }
                break;
            case 'm':
                o.matchNames.push_back(optarg);
                break;
            case 's':
                if (!parseInt(optarg, n) || n < 0 || n > 1024) return fail("--spawn must be 0..1024");
                o.spawnCount = static_cast<int>(n);
                break;
            case 'w':
                o.workerCmd = optarg;
                break;
            case kExitWithWorkers:
                o.exitWithWorkers = true;
                break;
            case kQueueCapacity:
                if (!parseInt(optarg, n) || n < 16) return fail("--queue-capacity must be >= 16");
                o.queueCapacity = static_cast<size_t>(n);
                break;
            case kProcRoot:
                o.procRoot = optarg;
                break;
            case 'q':
                o.quiet = true;
                break;
            case 'h':
                o.showHelp = true;
                break;
            case 'V':
                o.showVersion = true;
                break;
            case ':':
                return fail(std::string("missing argument for ") + storage[optind - 1]);
            default:
                return fail(std::string("unknown option ") + storage[optind - 1]);
        }
    }
    if (optind < argc) {
        return fail("unexpected argument: " + storage[optind]);
    }
    if (o.exitWithWorkers && o.spawnCount == 0) {
        return fail("--exit-with-workers requires --spawn");
    }
    return r;
}

}  // namespace systempulse

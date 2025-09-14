#include "systempulse/proc_parser.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <sstream>
#include <unordered_map>

namespace systempulse {

namespace {

uint64_t toU64(const std::string& s) {
    return static_cast<uint64_t>(std::strtoull(s.c_str(), nullptr, 10));
}

int64_t toI64(const std::string& s) {
    return static_cast<int64_t>(std::strtoll(s.c_str(), nullptr, 10));
}

std::vector<std::string> splitWhitespace(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream iss(line);
    std::string tok;
    while (iss >> tok) {
        out.push_back(tok);
    }
    return out;
}

// Parses "Key:   value [unit]" style files into key -> numeric value.
std::unordered_map<std::string, uint64_t> parseKeyValue(const std::string& content) {
    std::unordered_map<std::string, uint64_t> kv;
    std::istringstream iss(content);
    std::string line;
    while (std::getline(iss, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, colon);
        const auto tokens = splitWhitespace(line.substr(colon + 1));
        if (!tokens.empty()) {
            kv[key] = toU64(tokens[0]);
        }
    }
    return kv;
}

uint64_t lookup(const std::unordered_map<std::string, uint64_t>& kv, const char* key) {
    const auto it = kv.find(key);
    return it == kv.end() ? 0 : it->second;
}

bool allDigits(const std::string& s, size_t from) {
    if (from >= s.size()) {
        return false;
    }
    for (size_t i = from; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') {
            return false;
        }
    }
    return true;
}

bool startsWith(const std::string& s, const char* prefix) {
    return s.rfind(prefix, 0) == 0;
}

}  // namespace

std::optional<std::string> readFile(const std::string& path) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return std::nullopt;
    }
    std::string out;
    char buf[4096];
    for (;;) {
        const ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n > 0) {
            out.append(buf, static_cast<size_t>(n));
        } else if (n == 0) {
            break;
        } else if (errno == EINTR) {
            continue;
        } else {
            ::close(fd);
            return std::nullopt;
        }
    }
    ::close(fd);
    return out;
}

CpuSnapshot parseProcStat(const std::string& content) {
    CpuSnapshot snap;
    std::istringstream iss(content);
    std::string line;
    while (std::getline(iss, line)) {
        const auto tokens = splitWhitespace(line);
        if (tokens.empty()) {
            continue;
        }
        const std::string& key = tokens[0];
        if (startsWith(key, "cpu")) {
            CpuTimes t;
            t.label = key;
            uint64_t* fields[] = {&t.user, &t.nice,    &t.system,  &t.idle,
                                  &t.iowait, &t.irq, &t.softirq, &t.steal};
            // guest/guest_nice are already included in user/nice, so we stop at steal.
            for (size_t i = 0; i < 8 && i + 1 < tokens.size(); ++i) {
                *fields[i] = toU64(tokens[i + 1]);
            }
            snap.cpus.push_back(t);
        } else if (tokens.size() >= 2) {
            if (key == "ctxt") {
                snap.counters.contextSwitches = toU64(tokens[1]);
            } else if (key == "processes") {
                snap.counters.processesCreated = toU64(tokens[1]);
            } else if (key == "procs_running") {
                snap.counters.procsRunning = toU64(tokens[1]);
            } else if (key == "procs_blocked") {
                snap.counters.procsBlocked = toU64(tokens[1]);
            }
        }
    }
    return snap;
}

MemInfo parseMemInfo(const std::string& content) {
    const auto kv = parseKeyValue(content);
    MemInfo m;
    m.totalKb = lookup(kv, "MemTotal");
    m.freeKb = lookup(kv, "MemFree");
    m.buffersKb = lookup(kv, "Buffers");
    m.cachedKb = lookup(kv, "Cached");
    m.swapTotalKb = lookup(kv, "SwapTotal");
    m.swapFreeKb = lookup(kv, "SwapFree");
    m.dirtyKb = lookup(kv, "Dirty");
    if (kv.count("MemAvailable")) {
        m.availableKb = lookup(kv, "MemAvailable");
    } else {
        // Pre-3.14 kernels: approximate available memory.
        m.availableKb = m.freeKb + m.buffersKb + m.cachedKb;
    }
    return m;
}

std::vector<DiskStats> parseDiskStats(const std::string& content) {
    std::vector<DiskStats> out;
    std::istringstream iss(content);
    std::string line;
    while (std::getline(iss, line)) {
        const auto t = splitWhitespace(line);
        // major minor name + at least 11 stat fields
        if (t.size() < 14) {
            continue;
        }
        DiskStats d;
        d.device = t[2];
        d.readsCompleted = toU64(t[3]);
        d.sectorsRead = toU64(t[5]);
        d.readTimeMs = toU64(t[6]);
        d.writesCompleted = toU64(t[7]);
        d.sectorsWritten = toU64(t[9]);
        d.writeTimeMs = toU64(t[10]);
        d.ioInProgress = toU64(t[11]);
        d.ioTimeMs = toU64(t[12]);
        out.push_back(d);
    }
    return out;
}

std::optional<LoadAvg> parseLoadAvg(const std::string& content) {
    const auto t = splitWhitespace(content);
    if (t.size() < 4) {
        return std::nullopt;
    }
    LoadAvg l;
    l.one = std::strtod(t[0].c_str(), nullptr);
    l.five = std::strtod(t[1].c_str(), nullptr);
    l.fifteen = std::strtod(t[2].c_str(), nullptr);
    const auto slash = t[3].find('/');
    if (slash != std::string::npos) {
        l.runnable = static_cast<uint32_t>(toU64(t[3].substr(0, slash)));
        l.total = static_cast<uint32_t>(toU64(t[3].substr(slash + 1)));
    }
    return l;
}

std::optional<ProcStat> parsePidStat(const std::string& content) {
    // comm is wrapped in parentheses and may itself contain spaces or ')',
    // so split on the *last* closing paren.
    const auto open = content.find('(');
    const auto close = content.rfind(')');
    if (open == std::string::npos || close == std::string::npos || close < open) {
        return std::nullopt;
    }
    ProcStat s;
    s.pid = static_cast<int>(toI64(content.substr(0, open)));
    s.comm = content.substr(open + 1, close - open - 1);

    const auto t = splitWhitespace(content.substr(close + 1));
    // t[0] is field 3 (state) in proc(5) numbering; field N lives at t[N - 3].
    auto field = [&t](size_t n) -> const std::string& {
        static const std::string empty;
        return n - 3 < t.size() ? t[n - 3] : empty;
    };
    if (t.size() < 22) {
        return std::nullopt;
    }
    s.state = field(3).empty() ? '?' : field(3)[0];
    s.ppid = static_cast<int>(toI64(field(4)));
    s.minorFaults = toU64(field(10));
    s.majorFaults = toU64(field(12));
    s.utime = toU64(field(14));
    s.stime = toU64(field(15));
    s.numThreads = toI64(field(20));
    s.startTime = toU64(field(22));
    s.vsizeBytes = toU64(field(23));
    s.rssPages = toI64(field(24));
    if (!field(39).empty()) {
        s.processor = static_cast<int>(toI64(field(39)));
    }
    return s;
}

ProcStatus parsePidStatus(const std::string& content) {
    const auto kv = parseKeyValue(content);
    ProcStatus s;
    s.vmRssKb = lookup(kv, "VmRSS");
    s.vmHwmKb = lookup(kv, "VmHWM");
    s.voluntaryCtxSwitches = lookup(kv, "voluntary_ctxt_switches");
    s.nonvoluntaryCtxSwitches = lookup(kv, "nonvoluntary_ctxt_switches");
    return s;
}

ProcIo parsePidIo(const std::string& content) {
    const auto kv = parseKeyValue(content);
    ProcIo io;
    io.rchar = lookup(kv, "rchar");
    io.wchar = lookup(kv, "wchar");
    io.syscr = lookup(kv, "syscr");
    io.syscw = lookup(kv, "syscw");
    io.readBytes = lookup(kv, "read_bytes");
    io.writeBytes = lookup(kv, "write_bytes");
    io.cancelledWriteBytes = lookup(kv, "cancelled_write_bytes");
    return io;
}

bool isPhysicalDisk(const std::string& dev) {
    if (startsWith(dev, "loop") || startsWith(dev, "ram") || startsWith(dev, "zram") ||
        startsWith(dev, "dm-") || startsWith(dev, "sr")) {
        return false;
    }
    if (startsWith(dev, "nvme") || startsWith(dev, "mmcblk")) {
        // nvme0n1 is a disk, nvme0n1p1 is a partition; same for mmcblk0 / mmcblk0p1.
        const auto p = dev.rfind('p');
        return !(p != std::string::npos && p > 4 && allDigits(dev, p + 1));
    }
    if (startsWith(dev, "sd") || startsWith(dev, "vd") || startsWith(dev, "xvd") ||
        startsWith(dev, "hd")) {
        // sda is a disk, sda1 is a partition.
        return !dev.empty() && !(dev.back() >= '0' && dev.back() <= '9');
    }
    return false;
}

ProcReader::ProcReader(std::string procRoot) : root_(std::move(procRoot)) {}

std::string ProcReader::pidPath(int pid, const char* file) const {
    return root_ + "/" + std::to_string(pid) + "/" + file;
}

std::optional<CpuSnapshot> ProcReader::cpu() const {
    auto c = readFile(root_ + "/stat");
    if (!c) return std::nullopt;
    return parseProcStat(*c);
}

std::optional<MemInfo> ProcReader::memory() const {
    auto c = readFile(root_ + "/meminfo");
    if (!c) return std::nullopt;
    return parseMemInfo(*c);
}

std::optional<std::vector<DiskStats>> ProcReader::disks() const {
    auto c = readFile(root_ + "/diskstats");
    if (!c) return std::nullopt;
    return parseDiskStats(*c);
}

std::optional<LoadAvg> ProcReader::loadAvg() const {
    auto c = readFile(root_ + "/loadavg");
    if (!c) return std::nullopt;
    return parseLoadAvg(*c);
}

std::optional<ProcStat> ProcReader::pidStat(int pid) const {
    auto c = readFile(pidPath(pid, "stat"));
    if (!c) return std::nullopt;
    return parsePidStat(*c);
}

std::optional<ProcStatus> ProcReader::pidStatus(int pid) const {
    auto c = readFile(pidPath(pid, "status"));
    if (!c) return std::nullopt;
    return parsePidStatus(*c);
}

std::optional<ProcIo> ProcReader::pidIo(int pid) const {
    auto c = readFile(pidPath(pid, "io"));
    if (!c) return std::nullopt;
    return parsePidIo(*c);
}

bool ProcReader::pidExists(int pid) const {
    return ::access((root_ + "/" + std::to_string(pid)).c_str(), F_OK) == 0;
}

}  // namespace systempulse

#include "systempulse/metrics.h"
#include "systempulse/proc_parser.h"
#include "test_framework.h"

using namespace systempulse;

namespace {

const char* kProcStat =
    "cpu  1000 50 300 8000 200 10 40 0 0 0\n"
    "cpu0 500 25 150 4000 100 5 20 0 0 0\n"
    "cpu1 500 25 150 4000 100 5 20 0 0 0\n"
    "intr 123456 0 0\n"
    "ctxt 987654\n"
    "btime 1700000000\n"
    "processes 4321\n"
    "procs_running 3\n"
    "procs_blocked 1\n";

const char* kMemInfo =
    "MemTotal:       16000000 kB\n"
    "MemFree:         2000000 kB\n"
    "MemAvailable:    8000000 kB\n"
    "Buffers:          500000 kB\n"
    "Cached:          4000000 kB\n"
    "SwapTotal:       2000000 kB\n"
    "SwapFree:        1500000 kB\n"
    "Dirty:              1234 kB\n";

const char* kDiskStats =
    "   7       0 loop0 10 0 20 1 0 0 0 0 0 1 1 0 0 0 0\n"
    "   8       0 sda 1000 10 80000 500 2000 20 160000 1500 2 1800 2000 0 0 0 0\n"
    "   8       1 sda1 900 10 70000 450 1900 20 150000 1400 0 1700 1850 0 0 0 0\n";

const char* kPidStat =
    "4242 (my (weird) proc) S 1 4242 4242 0 -1 4194560 1500 0 7 0 "
    "250 50 0 0 20 0 4 0 123456 104857600 2560 18446744073709551615 "
    "1 1 0 0 0 0 0 0 0 0 0 0 17 3 0 0 0 0 0\n";

}  // namespace

TEST(parses_cpu_lines_and_counters) {
    const auto snap = parseProcStat(kProcStat);
    EXPECT_EQ(snap.cpus.size(), size_t{3});
    EXPECT_EQ(snap.cpus[0].label, std::string("cpu"));
    EXPECT_EQ(snap.cpus[0].user, uint64_t{1000});
    EXPECT_EQ(snap.cpus[0].iowait, uint64_t{200});
    EXPECT_EQ(snap.cpus[2].label, std::string("cpu1"));
    EXPECT_EQ(snap.counters.contextSwitches, uint64_t{987654});
    EXPECT_EQ(snap.counters.processesCreated, uint64_t{4321});
    EXPECT_EQ(snap.counters.procsRunning, uint64_t{3});
    EXPECT_EQ(snap.counters.procsBlocked, uint64_t{1});
}

TEST(cpu_usage_from_deltas) {
    CpuTimes prev{"cpu", 100, 0, 50, 800, 50, 0, 0, 0};
    CpuTimes cur{"cpu", 200, 0, 100, 1000, 100, 0, 0, 0};
    // delta: user 100, system 50, idle 200, iowait 50 -> total 400
    const auto u = computeCpuUsage(prev, cur);
    EXPECT_NEAR(u.userPct, 25.0, 1e-9);
    EXPECT_NEAR(u.systemPct, 12.5, 1e-9);
    EXPECT_NEAR(u.iowaitPct, 12.5, 1e-9);
    EXPECT_NEAR(u.busyPct, 37.5, 1e-9);
}

TEST(cpu_usage_handles_zero_delta) {
    CpuTimes t{"cpu", 1, 1, 1, 1, 1, 1, 1, 1};
    EXPECT_NEAR(computeCpuUsage(t, t).busyPct, 0.0, 1e-12);
}

TEST(parses_meminfo) {
    const auto m = parseMemInfo(kMemInfo);
    EXPECT_EQ(m.totalKb, uint64_t{16000000});
    EXPECT_EQ(m.availableKb, uint64_t{8000000});
    EXPECT_EQ(m.usedKb(), uint64_t{8000000});
    EXPECT_NEAR(m.usedPercent(), 50.0, 1e-9);
    EXPECT_EQ(m.swapUsedKb(), uint64_t{500000});
    EXPECT_EQ(m.dirtyKb, uint64_t{1234});
}

TEST(meminfo_falls_back_without_memavailable) {
    const auto m = parseMemInfo("MemTotal: 1000 kB\nMemFree: 100 kB\nBuffers: 50 kB\nCached: 150 kB\n");
    EXPECT_EQ(m.availableKb, uint64_t{300});
}

TEST(parses_diskstats) {
    const auto disks = parseDiskStats(kDiskStats);
    EXPECT_EQ(disks.size(), size_t{3});
    EXPECT_EQ(disks[1].device, std::string("sda"));
    EXPECT_EQ(disks[1].readsCompleted, uint64_t{1000});
    EXPECT_EQ(disks[1].sectorsWritten, uint64_t{160000});
    EXPECT_EQ(disks[1].ioTimeMs, uint64_t{1800});
}

TEST(disk_rates_from_deltas) {
    DiskStats prev;
    prev.device = "sda";
    DiskStats cur = prev;
    cur.readsCompleted = 100;
    cur.writesCompleted = 100;
    cur.sectorsRead = 2048;     // 1 MiB
    cur.sectorsWritten = 4096;  // 2 MiB
    cur.readTimeMs = 100;
    cur.writeTimeMs = 300;
    cur.ioTimeMs = 500;
    const auto r = computeDiskRates(prev, cur, 2.0);
    EXPECT_NEAR(r.readBytesPerSec, 524288.0, 1e-6);
    EXPECT_NEAR(r.writeBytesPerSec, 1048576.0, 1e-6);
    EXPECT_NEAR(r.readIops, 50.0, 1e-9);
    EXPECT_NEAR(r.utilizationPct, 25.0, 1e-9);
    EXPECT_NEAR(r.awaitMs, 2.0, 1e-9);
}

TEST(physical_disk_filter) {
    EXPECT_TRUE(isPhysicalDisk("sda"));
    EXPECT_FALSE(isPhysicalDisk("sda1"));
    EXPECT_TRUE(isPhysicalDisk("nvme0n1"));
    EXPECT_FALSE(isPhysicalDisk("nvme0n1p2"));
    EXPECT_TRUE(isPhysicalDisk("vda"));
    EXPECT_TRUE(isPhysicalDisk("mmcblk0"));
    EXPECT_FALSE(isPhysicalDisk("mmcblk0p1"));
    EXPECT_FALSE(isPhysicalDisk("loop3"));
    EXPECT_FALSE(isPhysicalDisk("dm-0"));
}

TEST(parses_loadavg) {
    const auto l = parseLoadAvg("0.52 0.58 0.59 2/1234 56789\n");
    EXPECT_TRUE(l.has_value());
    EXPECT_NEAR(l->one, 0.52, 1e-9);
    EXPECT_EQ(l->runnable, uint32_t{2});
    EXPECT_EQ(l->total, uint32_t{1234});
    EXPECT_FALSE(parseLoadAvg("garbage").has_value());
}

TEST(parses_pid_stat_with_parens_in_comm) {
    const auto s = parsePidStat(kPidStat);
    EXPECT_TRUE(s.has_value());
    EXPECT_EQ(s->pid, 4242);
    EXPECT_EQ(s->comm, std::string("my (weird) proc"));
    EXPECT_EQ(s->state, 'S');
    EXPECT_EQ(s->ppid, 1);
    EXPECT_EQ(s->minorFaults, uint64_t{1500});
    EXPECT_EQ(s->majorFaults, uint64_t{7});
    EXPECT_EQ(s->utime, uint64_t{250});
    EXPECT_EQ(s->stime, uint64_t{50});
    EXPECT_EQ(s->numThreads, int64_t{4});
    EXPECT_EQ(s->vsizeBytes, uint64_t{104857600});
    EXPECT_EQ(s->rssPages, int64_t{2560});
    EXPECT_EQ(s->processor, 3);
}

TEST(process_cpu_percent) {
    // 150 jiffies at 100 Hz over 1s = 150% (1.5 cores)
    EXPECT_NEAR(computeProcessCpuPercent(100, 250, 1.0, 100), 150.0, 1e-9);
    EXPECT_NEAR(computeProcessCpuPercent(250, 100, 1.0, 100), 0.0, 1e-9);
}

TEST(parses_pid_status_and_io) {
    const auto st = parsePidStatus(
        "Name:\tworker\nVmHWM:\t  20480 kB\nVmRSS:\t  10240 kB\n"
        "voluntary_ctxt_switches:\t42\nnonvoluntary_ctxt_switches:\t7\n");
    EXPECT_EQ(st.vmRssKb, uint64_t{10240});
    EXPECT_EQ(st.vmHwmKb, uint64_t{20480});
    EXPECT_EQ(st.voluntaryCtxSwitches, uint64_t{42});
    EXPECT_EQ(st.nonvoluntaryCtxSwitches, uint64_t{7});

    const auto io = parsePidIo(
        "rchar: 1000\nwchar: 2000\nsyscr: 10\nsyscw: 20\n"
        "read_bytes: 4096\nwrite_bytes: 8192\ncancelled_write_bytes: 0\n");
    EXPECT_EQ(io.rchar, uint64_t{1000});
    EXPECT_EQ(io.writeBytes, uint64_t{8192});
}

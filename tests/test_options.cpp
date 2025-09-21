#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "systempulse/options.h"
#include "systempulse/process_collector.h"
#include "systempulse/worker_pool.h"
#include "test_framework.h"

using namespace systempulse;

TEST(options_defaults) {
    const auto r = parseOptions({"systempulse"});
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.options.intervalMs, int64_t{1000});
    EXPECT_EQ(r.options.output, std::string("systempulse.csv"));
    EXPECT_TRUE(r.options.perCore);
    EXPECT_EQ(r.options.effective(r.options.cpuIntervalMs), int64_t{1000});
}

TEST(options_full_set) {
    const auto r = parseOptions({"systempulse", "-i", "250", "--cpu-interval", "100", "-d", "30",
                                 "-o", "-", "-p", "10,20", "--pid", "30", "-m", "nginx",
                                 "--devices", "sda,nvme0n1", "--no-per-core", "-q"});
    EXPECT_TRUE(r.ok);
    const auto& o = r.options;
    EXPECT_EQ(o.intervalMs, int64_t{250});
    EXPECT_EQ(o.effective(o.cpuIntervalMs), int64_t{100});
    EXPECT_EQ(o.effective(o.ioIntervalMs), int64_t{250});
    EXPECT_NEAR(o.durationSec, 30.0, 1e-9);
    EXPECT_EQ(o.output, std::string("-"));
    EXPECT_EQ(o.pids.size(), size_t{3});
    EXPECT_EQ(o.pids[2], 30);
    EXPECT_EQ(o.matchNames.size(), size_t{1});
    EXPECT_EQ(o.devices.count("nvme0n1"), size_t{1});
    EXPECT_FALSE(o.perCore);
    EXPECT_TRUE(o.quiet);
}

TEST(options_spawn_workers) {
    const auto r = parseOptions({"systempulse", "--spawn", "4", "--worker-cmd",
                                 "./pulse_worker --mode cpu", "--exit-with-workers"});
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.options.spawnCount, 4);
    EXPECT_TRUE(r.options.exitWithWorkers);
}

TEST(options_rejects_bad_input) {
    EXPECT_FALSE(parseOptions({"systempulse", "-i", "abc"}).ok);
    EXPECT_FALSE(parseOptions({"systempulse", "-i", "5"}).ok);
    EXPECT_FALSE(parseOptions({"systempulse", "--pid", "-3"}).ok);
    EXPECT_FALSE(parseOptions({"systempulse", "--bogus"}).ok);
    EXPECT_FALSE(parseOptions({"systempulse", "stray"}).ok);
    EXPECT_FALSE(parseOptions({"systempulse", "--exit-with-workers"}).ok);
    // Parser must be reusable after a failure.
    EXPECT_TRUE(parseOptions({"systempulse", "-i", "500"}).ok);
}

TEST(split_worker_command) {
    const auto v = WorkerPool::splitCommand("./pulse_worker --mode 'mixed load' --io-dir \"/tmp/x y\"");
    EXPECT_EQ(v.size(), size_t{5});
    EXPECT_EQ(v[2], std::string("mixed load"));
    EXPECT_EQ(v[4], std::string("/tmp/x y"));
}

TEST(find_pids_by_name_in_fake_proc) {
    char tmpl[] = "/tmp/systempulse_proc_XXXXXX";
    const char* root = mkdtemp(tmpl);
    EXPECT_TRUE(root != nullptr);
    auto makePid = [&](int pid, const char* comm) {
        const std::string dir = std::string(root) + "/" + std::to_string(pid);
        mkdir(dir.c_str(), 0755);
        FILE* f = std::fopen((dir + "/stat").c_str(), "w");
        std::fprintf(f,
                     "%d (%s) S 1 %d %d 0 -1 0 0 0 0 0 10 5 0 0 20 0 1 0 100 1000 10 0 "
                     "0 0 0 0 0 0 0 0 0 0 0 0 17 0 0 0 0 0 0\n",
                     pid, comm, pid, pid);
        std::fclose(f);
    };
    makePid(101, "worker");
    makePid(102, "other");
    makePid(103, "worker");
    mkdir((std::string(root) + "/self").c_str(), 0755);

    auto pids = ProcessCollector::findPidsByName(root, {"worker"});
    EXPECT_EQ(pids.size(), size_t{2});

    std::string cmd = std::string("rm -rf ") + root;
    EXPECT_EQ(std::system(cmd.c_str()), 0);
}

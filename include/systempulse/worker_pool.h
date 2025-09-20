// Spawns and supervises worker processes with fork/execvp/waitpid.
#pragma once

#include <sys/types.h>

#include <string>
#include <vector>

namespace systempulse {

struct WorkerInfo {
    int id = 0;
    pid_t pid = -1;
    bool running = false;
    int exitStatus = 0;  // raw waitpid status once reaped
    std::string label;
};

class WorkerPool {
public:
    explicit WorkerPool(std::vector<std::string> argv);
    ~WorkerPool();
    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    // Forks `count` workers. Each child gets SYSTEMPULSE_WORKER_ID / SYSTEMPULSE_WORKER_COUNT
    // in its environment and runs in its own process group. Call this while the
    // program is still single-threaded.
    bool spawn(int count);

    // Non-blocking reap of exited children; returns the number still running.
    int reap();

    // SIGTERM every live worker, wait up to graceMs, then SIGKILL stragglers.
    void terminateAll(int graceMs = 2000);

    const std::vector<WorkerInfo>& workers() const { return workers_; }
    int runningCount() const;

    static std::string describeStatus(int status);
    static std::vector<std::string> splitCommand(const std::string& cmd);

private:
    std::vector<std::string> argv_;
    std::vector<WorkerInfo> workers_;
};

}  // namespace systempulse

#include "systempulse/worker_pool.h"

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include "systempulse/sync.h"

namespace systempulse {

WorkerPool::WorkerPool(std::vector<std::string> argv) : argv_(std::move(argv)) {}

WorkerPool::~WorkerPool() { terminateAll(1000); }

std::vector<std::string> WorkerPool::splitCommand(const std::string& cmd) {
    // Whitespace split with support for simple single/double quoting.
    std::vector<std::string> out;
    std::string cur;
    char quote = 0;
    bool inToken = false;
    for (char c : cmd) {
        if (quote) {
            if (c == quote) {
                quote = 0;
            } else {
                cur += c;
            }
        } else if (c == '\'' || c == '"') {
            quote = c;
            inToken = true;
        } else if (c == ' ' || c == '\t') {
            if (inToken) {
                out.push_back(cur);
                cur.clear();
                inToken = false;
            }
        } else {
            cur += c;
            inToken = true;
        }
    }
    if (inToken) out.push_back(cur);
    return out;
}

bool WorkerPool::spawn(int count) {
    if (argv_.empty()) {
        std::fprintf(stderr, "systempulse: empty worker command\n");
        return false;
    }
    std::vector<char*> cargv;
    for (auto& a : argv_) cargv.push_back(const_cast<char*>(a.c_str()));
    cargv.push_back(nullptr);

    for (int i = 0; i < count; ++i) {
        const pid_t pid = fork();
        if (pid < 0) {
            std::fprintf(stderr, "systempulse: fork failed: %s\n", std::strerror(errno));
            return false;
        }
        if (pid == 0) {
            // Child: reset signal state inherited from the monitor, then exec.
            sigset_t empty;
            sigemptyset(&empty);
            sigprocmask(SIG_SETMASK, &empty, nullptr);
            signal(SIGINT, SIG_DFL);
            signal(SIGTERM, SIG_DFL);
            setpgid(0, 0);
            const std::string id = std::to_string(i);
            const std::string total = std::to_string(count);
            setenv("SYSTEMPULSE_WORKER_ID", id.c_str(), 1);
            setenv("SYSTEMPULSE_WORKER_COUNT", total.c_str(), 1);
            execvp(cargv[0], cargv.data());
            std::fprintf(stderr, "systempulse: exec %s failed: %s\n", cargv[0],
                         std::strerror(errno));
            _exit(127);
        }
        setpgid(pid, pid);  // also set from the parent to avoid the race
        WorkerInfo w;
        w.id = i;
        w.pid = pid;
        w.running = true;
        w.label = "worker-" + std::to_string(i);
        workers_.push_back(w);
    }
    return true;
}

int WorkerPool::reap() {
    for (auto& w : workers_) {
        if (!w.running) continue;
        int status = 0;
        const pid_t r = waitpid(w.pid, &status, WNOHANG);
        if (r == w.pid) {
            w.running = false;
            w.exitStatus = status;
        } else if (r < 0 && errno == ECHILD) {
            w.running = false;
        }
    }
    return runningCount();
}

int WorkerPool::runningCount() const {
    int n = 0;
    for (const auto& w : workers_) n += w.running ? 1 : 0;
    return n;
}

void WorkerPool::terminateAll(int graceMs) {
    if (reap() == 0) return;
    for (const auto& w : workers_) {
        if (w.running) kill(-w.pid, SIGTERM);  // whole process group
    }
    const double deadline = monotonicSeconds() + graceMs / 1000.0;
    while (reap() > 0 && monotonicSeconds() < deadline) {
        usleep(20 * 1000);
    }
    for (auto& w : workers_) {
        if (w.running) {
            kill(-w.pid, SIGKILL);
            int status = 0;
            waitpid(w.pid, &status, 0);
            w.running = false;
            w.exitStatus = status;
        }
    }
}

std::string WorkerPool::describeStatus(int status) {
    std::ostringstream os;
    if (WIFEXITED(status)) {
        os << "exited " << WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        os << "killed by signal " << WTERMSIG(status);
    } else {
        os << "status " << status;
    }
    return os.str();
}

}  // namespace systempulse

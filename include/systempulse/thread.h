// Minimal RAII wrapper around pthread_create / pthread_join.
#pragma once

#include <pthread.h>

#include <functional>
#include <string>

#include "systempulse/sync.h"

namespace systempulse {

class PThread {
public:
    PThread() = default;
    ~PThread();
    PThread(const PThread&) = delete;
    PThread& operator=(const PThread&) = delete;

    // Starts the thread; `name` is applied with pthread_setname_np where supported
    // (truncated to 15 chars on Linux) so threads are visible in `top -H` / `ps -T`.
    bool start(std::string name, std::function<void()> body);
    void join();
    bool running() const { return started_ && !joined_; }
    const std::string& name() const { return name_; }
    pthread_t native() const { return tid_; }

private:
    static void* trampoline(void* arg);

    pthread_t tid_{};
    std::function<void()> body_;
    std::string name_;
    bool started_ = false;
    bool joined_ = false;
};

// One-shot, broadcast stop flag that sleeping threads can wait on.
class StopSignal {
public:
    void request();
    bool requested() const;
    // Sleeps up to `ms`; returns true if stop was requested (early wakeup).
    bool waitFor(int64_t ms);
    // Sleeps until an absolute deadline on CondVar's clock.
    bool waitUntil(const timespec& deadline);

private:
    mutable Mutex mu_;
    CondVar cv_;
    bool stop_ = false;
};

}  // namespace systempulse

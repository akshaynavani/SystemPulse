// Thin RAII wrappers around pthread mutexes and condition variables.
#pragma once

#include <pthread.h>
#include <time.h>

#include <cstdint>

namespace systempulse {

class Mutex {
public:
    Mutex() { pthread_mutex_init(&m_, nullptr); }
    ~Mutex() { pthread_mutex_destroy(&m_); }
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

    void lock() { pthread_mutex_lock(&m_); }
    void unlock() { pthread_mutex_unlock(&m_); }
    pthread_mutex_t* native() { return &m_; }

private:
    pthread_mutex_t m_;
};

class LockGuard {
public:
    explicit LockGuard(Mutex& m) : m_(m) { m_.lock(); }
    ~LockGuard() { m_.unlock(); }
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;

private:
    Mutex& m_;
};

// Condition variable whose timed waits use CLOCK_MONOTONIC on Linux so that
// wall-clock adjustments (NTP, manual changes) don't stretch sampling intervals.
class CondVar {
public:
    CondVar();
    ~CondVar() { pthread_cond_destroy(&c_); }
    CondVar(const CondVar&) = delete;
    CondVar& operator=(const CondVar&) = delete;

    void wait(Mutex& m) { pthread_cond_wait(&c_, m.native()); }
    // Returns false on timeout.
    bool waitUntil(Mutex& m, const timespec& deadline);
    void signal() { pthread_cond_signal(&c_); }
    void broadcast() { pthread_cond_broadcast(&c_); }

    // "Now" on the same clock that waitUntil() deadlines use.
    static timespec now();
    static timespec addMillis(timespec t, int64_t ms);

private:
    pthread_cond_t c_;
};

// Clock helpers.
int64_t wallClockMillis();
double monotonicSeconds();

}  // namespace systempulse

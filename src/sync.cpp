#include "systempulse/sync.h"

namespace systempulse {

namespace {
#if defined(__linux__)
constexpr clockid_t kWaitClock = CLOCK_MONOTONIC;
#else
// pthread_condattr_setclock is not available everywhere (e.g. macOS); fall back
// to realtime so the code still compiles for local development.
constexpr clockid_t kWaitClock = CLOCK_REALTIME;
#endif
}  // namespace

CondVar::CondVar() {
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
#if defined(__linux__)
    pthread_condattr_setclock(&attr, kWaitClock);
#endif
    pthread_cond_init(&c_, &attr);
    pthread_condattr_destroy(&attr);
}

bool CondVar::waitUntil(Mutex& m, const timespec& deadline) {
    return pthread_cond_timedwait(&c_, m.native(), &deadline) == 0;
}

timespec CondVar::now() {
    timespec ts{};
    clock_gettime(kWaitClock, &ts);
    return ts;
}

timespec CondVar::addMillis(timespec t, int64_t ms) {
    t.tv_sec += static_cast<time_t>(ms / 1000);
    t.tv_nsec += static_cast<long>((ms % 1000) * 1000000L);
    if (t.tv_nsec >= 1000000000L) {
        t.tv_sec += 1;
        t.tv_nsec -= 1000000000L;
    }
    return t;
}

int64_t wallClockMillis() {
    timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

double monotonicSeconds() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1e9;
}

}  // namespace systempulse

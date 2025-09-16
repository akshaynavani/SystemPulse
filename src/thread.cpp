#include "systempulse/thread.h"

#include <cstdio>
#include <cstring>

namespace systempulse {

PThread::~PThread() { join(); }

void* PThread::trampoline(void* arg) {
    auto* self = static_cast<PThread*>(arg);
#if defined(__linux__)
    pthread_setname_np(pthread_self(), self->name_.substr(0, 15).c_str());
#elif defined(__APPLE__)
    pthread_setname_np(self->name_.c_str());
#endif
    self->body_();
    return nullptr;
}

bool PThread::start(std::string name, std::function<void()> body) {
    if (started_) {
        return false;
    }
    name_ = std::move(name);
    body_ = std::move(body);
    const int rc = pthread_create(&tid_, nullptr, &PThread::trampoline, this);
    if (rc != 0) {
        std::fprintf(stderr, "systempulse: pthread_create(%s) failed: %s\n", name_.c_str(),
                     std::strerror(rc));
        return false;
    }
    started_ = true;
    return true;
}

void PThread::join() {
    if (started_ && !joined_) {
        pthread_join(tid_, nullptr);
        joined_ = true;
    }
}

void StopSignal::request() {
    LockGuard g(mu_);
    stop_ = true;
    cv_.broadcast();
}

bool StopSignal::requested() const {
    LockGuard g(mu_);
    return stop_;
}

bool StopSignal::waitFor(int64_t ms) {
    return waitUntil(CondVar::addMillis(CondVar::now(), ms));
}

bool StopSignal::waitUntil(const timespec& deadline) {
    LockGuard g(mu_);
    while (!stop_) {
        if (!cv_.waitUntil(mu_, deadline)) {
            break;  // timed out
        }
    }
    return stop_;
}

}  // namespace systempulse

// Bounded multi-producer / multi-consumer queue built on pthread primitives.
#pragma once

#include <cstddef>
#include <deque>
#include <optional>
#include <vector>

#include "systempulse/sync.h"

namespace systempulse {

template <typename T>
class BoundedQueue {
public:
    enum class OverflowPolicy { Block, DropOldest };

    explicit BoundedQueue(size_t capacity, OverflowPolicy policy = OverflowPolicy::DropOldest)
        : capacity_(capacity == 0 ? 1 : capacity), policy_(policy) {}

    // Returns false if the queue is closed. With DropOldest, never blocks.
    bool push(T item) {
        LockGuard g(mu_);
        while (!closed_ && items_.size() >= capacity_ && policy_ == OverflowPolicy::Block) {
            notFull_.wait(mu_);
        }
        if (closed_) {
            return false;
        }
        if (items_.size() >= capacity_) {
            items_.pop_front();
            ++dropped_;
        }
        items_.push_back(std::move(item));
        notEmpty_.signal();
        return true;
    }

    // Blocks until an item is available or the queue is closed and drained.
    std::optional<T> pop() {
        LockGuard g(mu_);
        while (items_.empty() && !closed_) {
            notEmpty_.wait(mu_);
        }
        if (items_.empty()) {
            return std::nullopt;
        }
        T item = std::move(items_.front());
        items_.pop_front();
        notFull_.signal();
        return item;
    }

    // Blocks for at least one item, then takes up to maxItems. Empty result means
    // closed and drained.
    std::vector<T> popBatch(size_t maxItems) {
        std::vector<T> out;
        LockGuard g(mu_);
        while (items_.empty() && !closed_) {
            notEmpty_.wait(mu_);
        }
        while (!items_.empty() && out.size() < maxItems) {
            out.push_back(std::move(items_.front()));
            items_.pop_front();
        }
        if (!out.empty()) {
            notFull_.broadcast();
        }
        return out;
    }

    std::optional<T> tryPop() {
        LockGuard g(mu_);
        if (items_.empty()) {
            return std::nullopt;
        }
        T item = std::move(items_.front());
        items_.pop_front();
        notFull_.signal();
        return item;
    }

    void close() {
        LockGuard g(mu_);
        closed_ = true;
        notEmpty_.broadcast();
        notFull_.broadcast();
    }

    size_t size() const {
        LockGuard g(mu_);
        return items_.size();
    }

    size_t dropped() const {
        LockGuard g(mu_);
        return dropped_;
    }

    bool closed() const {
        LockGuard g(mu_);
        return closed_;
    }

    size_t capacity() const { return capacity_; }

private:
    const size_t capacity_;
    const OverflowPolicy policy_;
    mutable Mutex mu_;
    CondVar notEmpty_;
    CondVar notFull_;
    std::deque<T> items_;
    size_t dropped_ = 0;
    bool closed_ = false;
};

}  // namespace systempulse

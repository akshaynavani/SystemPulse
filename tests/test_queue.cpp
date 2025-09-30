#include <atomic>
#include <memory>
#include <vector>

#include "systempulse/csv_writer.h"
#include "systempulse/queue.h"
#include "systempulse/thread.h"
#include "test_framework.h"

using namespace systempulse;

TEST(queue_fifo_order) {
    BoundedQueue<int> q(8);
    for (int i = 0; i < 5; ++i) q.push(i);
    for (int i = 0; i < 5; ++i) {
        const int v = q.pop().value();
        EXPECT_EQ(v, i);
    }
    EXPECT_FALSE(q.tryPop().has_value());
}

TEST(queue_drop_oldest_when_full) {
    BoundedQueue<int> q(3, BoundedQueue<int>::OverflowPolicy::DropOldest);
    for (int i = 0; i < 5; ++i) q.push(i);
    EXPECT_EQ(q.size(), size_t{3});
    EXPECT_EQ(q.dropped(), size_t{2});
    const int oldest = q.pop().value();
    EXPECT_EQ(oldest, 2);
}

TEST(queue_close_unblocks_consumer) {
    BoundedQueue<int> q(4);
    std::atomic<bool> gotNullopt{false};
    PThread consumer;
    consumer.start("test-consumer", [&] { gotNullopt = !q.pop().has_value(); });
    StopSignal s;
    s.waitFor(20);
    q.close();
    consumer.join();
    EXPECT_TRUE(gotNullopt.load());
    EXPECT_FALSE(q.push(1));
}

TEST(queue_many_producers_one_consumer) {
    constexpr int kProducers = 4;
    constexpr int kPerProducer = 5000;
    BoundedQueue<int> q(64, BoundedQueue<int>::OverflowPolicy::Block);

    std::atomic<long long> sum{0};
    std::atomic<int> count{0};
    PThread consumer;
    consumer.start("test-consumer", [&] {
        for (;;) {
            auto batch = q.popBatch(32);
            if (batch.empty()) break;
            for (int v : batch) {
                sum += v;
                ++count;
            }
        }
    });

    std::vector<std::unique_ptr<PThread>> producers;
    for (int p = 0; p < kProducers; ++p) {
        producers.push_back(std::make_unique<PThread>());
        producers.back()->start("test-producer", [&q] {
            for (int i = 1; i <= kPerProducer; ++i) q.push(i);
        });
    }
    for (auto& p : producers) p->join();
    q.close();
    consumer.join();

    EXPECT_EQ(count.load(), kProducers * kPerProducer);
    const long long expected = static_cast<long long>(kProducers) * kPerProducer * (kPerProducer + 1) / 2;
    EXPECT_EQ(sum.load(), expected);
    EXPECT_EQ(q.dropped(), size_t{0});
}

TEST(stop_signal_wakes_early) {
    StopSignal s;
    PThread t;
    t.start("test-stopper", [&] {
        StopSignal nap;
        nap.waitFor(10);
        s.request();
    });
    const double start = monotonicSeconds();
    EXPECT_TRUE(s.waitFor(5000));
    EXPECT_TRUE(monotonicSeconds() - start < 2.0);
    t.join();
}

TEST(csv_row_formatting_and_escaping) {
    Sample s(1700000000123, "proc", "pid:42:my,proc", "cpu_pct", 12.5);
    EXPECT_EQ(CsvWriter::formatRow(s),
              std::string("1700000000123,proc,\"pid:42:my,proc\",cpu_pct,12.5"));
    EXPECT_EQ(CsvWriter::escapeField("a\"b"), std::string("\"a\"\"b\""));
    EXPECT_EQ(CsvWriter::escapeField("plain"), std::string("plain"));
}

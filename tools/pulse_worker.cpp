// pulse_worker: synthetic CPU / memory / IO load generator used to benchmark
// SystemPulse. Each worker runs one or more pthreads doing the selected work
// until --duration elapses or it receives SIGTERM/SIGINT.
//
//   pulse_worker --mode cpu   --threads 2 --cpu-load 75
//   pulse_worker --mode mem   --mem-mb 256
//   pulse_worker --mode io    --io-mb 64 --io-dir /tmp
//   pulse_worker --mode mixed --duration 30

#include <fcntl.h>
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

volatile sig_atomic_t g_stop = 0;

void onSignal(int) { g_stop = 1; }

struct Config {
    std::string mode = "mixed";
    int threads = 1;
    int cpuLoad = 100;  // duty cycle percentage per CPU thread
    int memMb = 128;
    int ioMb = 32;
    std::string ioDir = "/tmp";
    double duration = 0;  // seconds, 0 = until signalled
    int workerId = 0;
};

double nowSec() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

void sleepMs(long ms) {
    timespec ts{ms / 1000, (ms % 1000) * 1000000L};
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR && !g_stop) {
    }
}

std::atomic<uint64_t> g_sink{0};
std::atomic<int> g_ioThreadSeq{0};

// Busy-spins for cpuLoad% of every 100ms period.
void* cpuWorker(void* arg) {
    const Config& cfg = *static_cast<Config*>(arg);
    const double period = 0.1;
    const double busy = period * cfg.cpuLoad / 100.0;
    double x = 1.0;
    while (!g_stop) {
        const double start = nowSec();
        while (!g_stop && nowSec() - start < busy) {
            for (int i = 0; i < 2000; ++i) x = std::sqrt(x * 1.000001 + i);
        }
        g_sink += static_cast<uint64_t>(x);
        const double rest = period - (nowSec() - start);
        if (rest > 0) sleepMs(static_cast<long>(rest * 1000));
    }
    return nullptr;
}

// Grows an allocation to memMb in 8 MiB steps, touching every page so it counts
// toward RSS, then keeps rewriting it; releases and regrows to create a sawtooth.
void* memWorker(void* arg) {
    const Config& cfg = *static_cast<Config*>(arg);
    const size_t chunk = 8u << 20;
    const long page = sysconf(_SC_PAGESIZE) > 0 ? sysconf(_SC_PAGESIZE) : 4096;
    while (!g_stop) {
        std::vector<char*> blocks;
        size_t total = 0;
        while (!g_stop && total < static_cast<size_t>(cfg.memMb) << 20) {
            char* b = static_cast<char*>(std::malloc(chunk));
            if (!b) break;
            for (size_t off = 0; off < chunk; off += page) b[off] = static_cast<char>(off);
            blocks.push_back(b);
            total += chunk;
            sleepMs(50);
        }
        for (int pass = 0; pass < 20 && !g_stop; ++pass) {
            for (char* b : blocks) {
                for (size_t off = 0; off < chunk; off += page) b[off]++;
            }
            sleepMs(100);
        }
        for (char* b : blocks) std::free(b);
    }
    return nullptr;
}

// Writes ioMb to a scratch file with fsync, then reads it back after dropping
// it from the page cache so the reads hit the block device.
void* ioWorker(void* arg) {
    const Config& cfg = *static_cast<Config*>(arg);
    char path[512];
    std::snprintf(path, sizeof(path), "%s/pulse_worker_%d_%d.dat", cfg.ioDir.c_str(),
                  static_cast<int>(getpid()), g_ioThreadSeq.fetch_add(1));
    std::vector<char> buf(1 << 20);
    for (size_t i = 0; i < buf.size(); ++i) buf[i] = static_cast<char>(i * 31);

    while (!g_stop) {
        int fd = ::open(path, O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0600);
        if (fd < 0) {
            std::perror("pulse_worker: open for write");
            return nullptr;
        }
        for (int mb = 0; mb < cfg.ioMb && !g_stop; ++mb) {
            if (::write(fd, buf.data(), buf.size()) < 0) break;
            if (mb % 8 == 7) fsync(fd);
        }
        fsync(fd);
#ifdef POSIX_FADV_DONTNEED
        posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
#endif
        ::close(fd);

        fd = ::open(path, O_RDONLY | O_CLOEXEC);
        if (fd >= 0) {
            while (!g_stop && ::read(fd, buf.data(), buf.size()) > 0) {
            }
            ::close(fd);
        }
        sleepMs(200);
    }
    ::unlink(path);
    return nullptr;
}

void usage(const char* prog) {
    std::fprintf(stderr,
                 "usage: %s [--mode cpu|mem|io|mixed] [--threads N] [--cpu-load PCT]\n"
                 "          [--mem-mb MB] [--io-mb MB] [--io-dir DIR] [--duration SEC]\n",
                 prog);
}

bool parse(int argc, char** argv, Config& cfg) {
    static const option opts[] = {
        {"mode", required_argument, nullptr, 'm'},     {"threads", required_argument, nullptr, 't'},
        {"cpu-load", required_argument, nullptr, 'c'}, {"mem-mb", required_argument, nullptr, 'M'},
        {"io-mb", required_argument, nullptr, 'I'},    {"io-dir", required_argument, nullptr, 'D'},
        {"duration", required_argument, nullptr, 'd'}, {"help", no_argument, nullptr, 'h'},
        {nullptr, 0, nullptr, 0}};
    int c;
    while ((c = getopt_long(argc, argv, "m:t:c:M:I:D:d:h", opts, nullptr)) != -1) {
        switch (c) {
            case 'm': cfg.mode = optarg; break;
            case 't': cfg.threads = std::max(1, std::atoi(optarg)); break;
            case 'c': cfg.cpuLoad = std::min(100, std::max(1, std::atoi(optarg))); break;
            case 'M': cfg.memMb = std::max(8, std::atoi(optarg)); break;
            case 'I': cfg.ioMb = std::max(1, std::atoi(optarg)); break;
            case 'D': cfg.ioDir = optarg; break;
            case 'd': cfg.duration = std::atof(optarg); break;
            default: return false;
        }
    }
    if (cfg.mode != "cpu" && cfg.mode != "mem" && cfg.mode != "io" && cfg.mode != "mixed") {
        std::fprintf(stderr, "pulse_worker: unknown mode '%s'\n", cfg.mode.c_str());
        return false;
    }
    if (const char* id = std::getenv("SYSTEMPULSE_WORKER_ID")) cfg.workerId = std::atoi(id);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Config cfg;
    if (!parse(argc, argv, cfg)) {
        usage(argv[0]);
        return 2;
    }

    struct sigaction sa {};
    sa.sa_handler = onSignal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);

    std::vector<pthread_t> threads;
    auto launch = [&](void* (*fn)(void*)) {
        pthread_t t;
        if (pthread_create(&t, nullptr, fn, &cfg) == 0) threads.push_back(t);
    };
    for (int i = 0; i < cfg.threads; ++i) {
        if (cfg.mode == "cpu") {
            launch(cpuWorker);
        } else if (cfg.mode == "mem") {
            launch(memWorker);
        } else if (cfg.mode == "io") {
            launch(ioWorker);
        } else {
            // mixed: rotate by worker id so a pool of workers gets a varied profile
            void* (*fns[])(void*) = {cpuWorker, memWorker, ioWorker};
            launch(fns[(cfg.workerId + i) % 3]);
        }
    }

    const double start = nowSec();
    while (!g_stop) {
        if (cfg.duration > 0 && nowSec() - start >= cfg.duration) {
            g_stop = 1;
            break;
        }
        sleepMs(100);
    }
    for (pthread_t t : threads) pthread_join(t, nullptr);
    std::fprintf(stderr, "pulse_worker[%d] %s mode finished after %.1fs\n", cfg.workerId,
                 cfg.mode.c_str(), nowSec() - start);
    return 0;
}

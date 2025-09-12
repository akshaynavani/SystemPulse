# SystemPulse — Implementation Plan

**SystemPulse** is a Linux system resource monitor written in C++17. It uses pthreads and POSIX
APIs to concurrently sample CPU, memory, and IO metrics — both system-wide and for a set of
worker processes — by parsing the `/proc` filesystem. Samples are streamed to CSV, Bash scripts
automate benchmark runs and reporting, and a Python pipeline performs time-series analysis.

## Key design decisions

- **Language/build:** C++17, CMake, no third-party C++ dependencies. Threads use raw `pthread_*`
  APIs (not `std::thread`) to stay close to POSIX, wrapped in small RAII helpers.
- **Data source:** `/proc/stat`, `/proc/meminfo`, `/proc/diskstats`, `/proc/loadavg`,
  `/proc/<pid>/stat`, `/proc/<pid>/status`, `/proc/<pid>/io`. Parsers are pure functions that take
  file contents as strings so they can be unit-tested with fixtures on any OS.
- **Concurrency model:** one collector thread per metric domain (cpu, memory, io, processes), each
  sampling on its own interval and pushing `Sample` records into a bounded, mutex + condvar
  protected queue. A single writer thread drains the queue to CSV. Shutdown is driven by
  `SIGINT`/`SIGTERM` handled with `sigwait` on a dedicated signal thread.
- **Distributed workers:** the monitor can either attach to existing PIDs (`--pid`) or spawn N
  worker processes itself (`--spawn N --worker-cmd "..."`) with `fork`/`execvp`, then track each
  worker's CPU%, RSS, and read/write bytes. A bundled `pulse_worker` binary generates synthetic
  CPU / memory / IO load for benchmarking.
- **Output format:** long-format CSV: `timestamp_ms,source,entity,metric,value`. Easy to append,
  easy to pivot in pandas.
- **Analysis:** Python 3 (pandas, numpy, matplotlib) — resampling, rolling stats, percentiles,
  z-score anomaly detection, per-worker comparisons, Markdown report + PNG plots.

## Phase 1 — Project skeleton and /proc parsers

Deliverables:
- CMake build with `systempulse` library, `systempulse` executable placeholder, and tests target.
- Metric types (`CpuTimes`, `MemInfo`, `DiskStats`, `ProcStat`, `ProcIo`, `LoadAvg`).
- Pure parsers for each `/proc` file plus file-reading helpers; delta/percent calculations
  (CPU utilization from jiffies deltas, IO throughput from sector deltas).
- Minimal self-contained unit-test runner and parser tests using fixture strings.

Files: `CMakeLists.txt`, `include/systempulse/metrics.h`, `include/systempulse/proc_parser.h`,
`src/proc_parser.cpp`, `src/metrics.cpp`, `tests/test_main.cpp`, `tests/test_proc_parser.cpp`.

## Phase 2 — Multithreaded collection engine

Deliverables:
- `BoundedQueue<T>` built on `pthread_mutex_t` / `pthread_cond_t`.
- `PThread` RAII wrapper and `Collector` base class with interval loop and stop flag.
- Collectors: `CpuCollector` (total + per-core), `MemoryCollector`, `IoCollector` (per-device).
- `CsvWriter` thread draining the queue; `Monitor` orchestrator; signal-handling thread.
- Queue unit tests.

Files: `include/systempulse/{queue,thread,collector,collectors,csv_writer,monitor,sample}.h`,
`src/{thread,collectors,csv_writer,monitor}.cpp`, `tests/test_queue.cpp`.

## Phase 3 — Worker process tracking and CLI

Deliverables:
- `ProcessCollector` tracking a dynamic PID set (CPU%, RSS, threads, read/write bytes, ctx switches).
- `WorkerPool` that spawns workers with `fork`/`execvp`, reaps with `waitpid`, terminates cleanly.
- `pulse_worker` synthetic load generator (cpu / mem / io / mixed modes, pthreads inside).
- CLI (`getopt_long`) in `src/main.cpp`: interval, duration, output path, pids, spawn options.

Files: `include/systempulse/{process_collector,worker_pool,options}.h`,
`src/{process_collector,worker_pool,options,main}.cpp`, `tools/pulse_worker.cpp`,
`tests/test_options.cpp`.

## Phase 4 — Bash automation

Deliverables:
- `scripts/build.sh` — configure + build + run unit tests.
- `scripts/run_benchmark.sh` — run a matrix of workload scenarios, one CSV per run, with metadata.
- `scripts/report.sh` — quick shell-side summary (awk) and invoke the Python pipeline.
- `scripts/lib.sh` — shared logging / prerequisite checks.

## Phase 5 — Python time-series analysis pipeline

Deliverables:
- `analysis/systempulse_analysis/` package: loader (long CSV -> wide time series), stats
  (resampling, rolling means, p50/p95/p99), anomaly detection (rolling z-score), plotting,
  Markdown report generation, CLI entry point.
- `analysis/tests/` pytest suite with synthetic data; `requirements.txt`.

Files: `analysis/systempulse_analysis/{__init__,loader,stats,anomaly,plots,report,cli}.py`,
`analysis/tests/test_*.py`, `analysis/requirements.txt`.

## Wrap-up

`.gitignore` for CMake/C++/Python artifacts, `README.md` with architecture diagram and run/test
instructions.

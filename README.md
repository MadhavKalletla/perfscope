# perfscope: Real-Time Process & System Performance Profiler

[![CI](https://github.com/your-username/perfscope/actions/workflows/ci.yml/badge.svg)](https://github.com/your-username/perfscope/actions/workflows/ci.yml)
![Language](https://img.shields.io/badge/Language-C%2B%2B17-blue.svg)
![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux-brightgreen.svg)
![License](https://img.shields.io/badge/License-MIT-green.svg)

`perfscope` is a lightweight, high-performance process and system profiler written in modern **C++17**. Designed specifically to demonstrate deep operating systems internals and performance engineering concepts relevant to **Windows Device Performance and Kernel/OS teams**, it features a native Win32 platform implementation with a transparent `/proc` fallback for Linux.

---

## Features

1. **`perfscope watch`**: Point-in-time process telemetry sampling.
   - Measures normalized **CPU %**, **Working Set** (physical RAM resident pages), **Private Bytes** (committed non-shareable memory), **Thread Count**, and kernel **Handle Count**.
   - Streams live metrics to console and writes structured CSV traces.
   - Graceful termination on `Ctrl+C` preserves partial traces.
2. **`perfscope top`**: High-performance system process monitor.
   - Enumerate all active system processes with snapshot inspection.
   - Sort dynamically by CPU consumption or physical working set memory.
   - Sub-second execution using optimized single-pass kernel snapshotting.
3. **`perfscope analyze`**: Statistical analysis and heuristic anomaly detection.
   - Calculates **Min, Mean, Median (p50), 95th Percentile (p95), 99th Percentile (p99), Max**, and **Standard Deviation** for all metrics.
   - **Sudden CPU Spike Detection**: Identifies sharp deltas ($\ge 40\%$) and statistical outliers ($\mu + 2.5\sigma$).
   - **Steady Memory Growth (Leak Detection)**: Uses Ordinary Least Squares (OLS) linear regression over time to detect sustained leaks ($R^2 \ge 0.75$, positive slope in MB/min).
4. **`perfscope bench`**: Reproducible built-in workload harness.
   - **CPU-bound multi-threaded workload**: saturates all logical hardware threads with compute loops.
   - **Memory allocation & physical page faulting**: commits virtual memory in chunks and forces physical page frame allocation by touching 4KB pages.
   - **Lock contention benchmark**: benchmarks `std::mutex` vs `std::atomic<uint64_t>` across contending worker threads, measuring latency and speedup.

---

## Architectural Design

`perfscope` is built with a modular, layered architecture adhering to the Single Responsibility Principle:

```
                            +-----------------------+
                            |       perfscope       |
                            |       CLI Driver      |
                            +-----------+-----------+
                                        |
       +-----------------+--------------+--------------+-----------------+
       |                 |                             |                 |
+------v------+   +------v------+               +------v------+   +------v------+
|   Sampler   |   |   CSV I/O   |               |    Stats    |   |  Benchmark  |
|  Interface  |   | (Streaming) |               |  & Anomaly  |   |   Harness   |
+------+------+   +-------------+               +-------------+   +-------------+
       |
  +----+----+
  |         |
+-v---+   +-v---+
|Win32|   |Linux|
|API  |   |/proc|
+-----+   +-----+
```

### Platform Abstraction
- The abstract interface `ProcessSampler` decouples metric collection from downstream consumption.
- **Windows Implementation (`WindowsSampler`)**:
  - `GetProcessTimes`: Queries kernel mode and user mode CPU ticks in 100-nanosecond intervals. CPU % is normalized across wall-clock delta and logical processor count (`GetSystemInfo`).
  - `GetProcessMemoryInfo` (`PROCESS_MEMORY_COUNTERS_EX`): Captures `WorkingSetSize` vs `PagefileUsage` (`PrivateUsage`).
  - `GetProcessHandleCount`: Tracks open Win32 kernel handles (files, events, mutexes, threads, tokens).
  - `CreateToolhelp32Snapshot`: Inspects thread topologies and enumerates running processes.
- **Linux Fallback (`LinuxSampler`)**:
  - Reads `/proc/[pid]/stat` for `utime`, `stime`, and thread count.
  - Reads `/proc/[pid]/status` for `VmRSS` (working set) and `VmData` (private committed pages).
  - Reads `/proc/[pid]/fd` count for open file descriptors.

---

## Building the Project

### Prerequisites
- **C++17 Compiler**: MSVC (VS 2019/2022), GCC 9+, or Clang 10+
- **CMake**: Version 3.16 or newer
- **Ninja** or **Make** (or Visual Studio project generator)

### Build on Windows (Visual Studio / MSVC)
```powershell
cmake -B build
cmake --build build --config Release
```

### Build on Windows (MinGW-w64 / MSYS2 / GCC)
```powershell
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Build on Linux (Ubuntu / Debian)
```bash
sudo apt-get update && sudo apt-get install -y cmake ninja-build g++
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Running Tests
`perfscope` uses the Catch2 test framework for unit testing:
```bash
ctest --test-dir build --output-on-failure
# Or directly run the binary:
./build/perfscope_tests
```

---

## Usage Examples

### 1. Watch a Target Process
Sample process PID 23796 every 500 ms for 30 seconds and output to `explorer_trace.csv`:
```powershell
perfscope watch --pid 23796 --interval 500 --duration 30 --output explorer_trace.csv
```

Console Output:
```
Target: explorer.exe (PID: 23796)
Sampling interval: 500 ms | Duration: 30 s
Output CSV: explorer_trace.csv

Timestamp (ISO)               CPU %     Working Set   Private Bytes   Threads   Handles
---------------------------------------------------------------------------------------
2026-10-18T18:43:00.318Z       0.0%       332.58 MB       354.25 MB       101      5859
2026-10-18T18:43:00.883Z       0.0%       332.59 MB       354.25 MB       101      5859
2026-10-18T18:43:01.472Z       0.4%       332.59 MB       354.25 MB       101      5859
2026-10-18T18:43:02.089Z       2.1%       332.64 MB       354.27 MB       101      5913
2026-10-18T18:43:02.668Z       0.7%       332.66 MB       354.28 MB       101      5887
2026-10-18T18:43:03.247Z       0.0%       332.66 MB       354.28 MB       101      5887

Finished sampling: 60 samples saved to explorer_trace.csv
```

### 2. View Top System Processes
List top 8 processes sorted by memory consumption:
```powershell
perfscope top --limit 8 --sort mem
```

Console Output:
```
PID     Process Name                     CPU %     Working Set   Private Bytes   Threads   Handles
==================================================================================================
6664    chrome.exe                        4.2%       490.64 MB       607.77 MB        26       394
14696   Antigravity IDE.exe               9.5%       460.41 MB       637.50 MB        25       631
3640    chrome.exe                        0.1%       451.20 MB       538.66 MB        27       506
13572   chrome.exe                        0.8%       438.91 MB       326.41 MB        50      4895
2808    chrome.exe                        0.0%       340.09 MB       420.95 MB        26       405
23796   explorer.exe                      0.1%       332.73 MB       354.63 MB       107      5887
9840    chrome.exe                        0.0%       325.90 MB       318.12 MB        27       424
2856    chrome.exe                        0.0%       304.29 MB       326.07 MB        26       372

Showing top 8 of 352 processes (sorted by mem).
```

### 3. Analyze a CSV Trace
Compute percentiles and scan for performance anomalies:
```powershell
perfscope analyze explorer_trace.csv
```

Console Output:
```
Loading CSV file: explorer_trace.csv
Loaded 60 samples (Total lines read: 60, Malformed rows skipped: 0).

========================================================================================================
                                     STATISTICAL METRICS SUMMARY                                        
========================================================================================================
Metric                     Min        Mean   p50 (Med)         p95         p99         Max      StdDev
--------------------------------------------------------------------------------------------------------
CPU %                     0.0%        0.5%        0.2%        1.8%        2.0%        2.1%        0.8%
Working Set          332.58 MB   332.62 MB   332.62 MB   332.66 MB   332.66 MB   332.66 MB    35.58 KB
Private Bytes        354.25 MB   354.26 MB   354.26 MB   354.28 MB   354.28 MB   354.28 MB    18.07 KB
Threads                  101.0       101.0       101.0       101.0       101.0       101.0         0.0
Handles                 5859.0      5877.3      5873.0      5906.5      5911.7      5913.0        22.2
========================================================================================================

========================================================================================================
                                     ANOMALY DETECTION REPORT                                           
========================================================================================================
No anomalies detected. Process behavior appears stable and within normal baseline parameters.
========================================================================================================
```

If anomalies exist, `perfscope` reports the exact timestamp and documented rule trigger:
```
Identified 2 anomaly event(s):

 [1] Sudden CPU Spike
     Description: CPU surged by +76.5% (from 5.5% to 82.0%) at 2026-10-18T18:00:02.000Z
     Rule Logic:  Rule: Consecutive sample delta (76.5%) exceeds threshold (40%).

 [2] Steady Memory Growth (Suspected Leak)
     Description: Private memory grew by 5.00 MB at an estimated rate of +120.00 MB/min (from 20.00 MB to 25.00 MB).
     Rule Logic:  Rule: OLS Linear Regression slope (2.00 MB/s) >= 48.83 KB/s AND goodness of fit R² (1.000) >= 0.750.
```

### 4. Run Built-In Benchmarks
Run hardware-level microbenchmarks directly:
```powershell
perfscope bench
```

---

## Benchmark Results

> **HONESTY POLICY**: Benchmark numbers vary by CPU architecture, core count, thermal throttling, and OS scheduling state. Below is an authentic run on our test hardware, followed by a template placeholder to paste your own measured machine results.

### Measured Run on Author's Test System
- **CPU**: AMD/Intel x86_64 (12 Logical Cores)
- **OS**: Windows 11 Enterprise (Build 26200)

```
=======================================================
       perfscope System Performance Benchmark Suite    
=======================================================

[1/3] Running CPU-bound multi-threaded workload (3 seconds)...
      Threads utilized:  12
      Total operations:  6901785000
      Throughput:        2291.33 Million Ops/sec

[2/3] Running Memory Allocation & Physical Page Touch workload (100 MB)...
      Allocated & Touched: 100.00 MB
      4KB Pages faulted:   25600 pages
      Allocation Rate:     43.61 MB/sec

[3/3] Running Multi-threaded Lock Contention (Mutex vs Atomics)...
      Contending Threads:  12
      Total Operations:    3999996
      std::mutex Duration: 258.92 ms (15.45 M ops/sec)
      std::atomic Duration:80.32 ms (49.80 M ops/sec)
      Atomic Speedup:      3.22x faster than mutex

=======================================================
Benchmark completed successfully.
```

---

### [PASTE YOUR BENCHMARK RESULTS HERE]

```text
<!-- 
Run `perfscope bench` on your machine and paste the output below:
-->

[PASTE OUTPUT FROM `perfscope bench` HERE]
```

---

## Key Performance Design Decisions

1. **Working Set vs. Private Bytes**:
   - Many rudimentary tools only query "memory" as a single scalar. On modern virtual memory operating systems, `WorkingSet` is the set of pages resident in physical DRAM; under system memory pressure, the Windows Working Set Manager (WSM) trims clean/modified pages to the pagefile.
   - `Private Bytes` (`PagefileUsage` in Win32) represents memory committed exclusively to this process that cannot be shared with other processes. A true software memory leak shows steadily climbing Private Bytes even if the Working Set fluctuates.
2. **Normalized CPU Calculation**:
   - `GetProcessTimes` returns time spent in Kernel Mode and User Mode in 100-nanosecond slices.
   - `perfscope` tracks steady-clock wall time $\Delta t$ between samples and normalizes by hardware logical cores $C$:
     $$\text{CPU \%} = \frac{\Delta \text{KernelTime} + \Delta \text{UserTime}}{\Delta t \times C} \times 100$$
3. **Linear Regression for Memory Leak Detection**:
   - Simple threshold rules ("is memory higher than at step 1?") cause false alarms during normal heap fragmentation or initial startup caching.
   - `perfscope` applies Ordinary Least Squares (OLS) linear regression across private bytes over time, checking both the rate of growth (slope $\ge 50$ KB/s) and goodness-of-fit correlation ($R^2 \ge 0.75$).
4. **Lock Contention Microbenchmark**:
   - Demonstrates the cost of OS kernel transitions. `std::mutex` contends in user space and drops into kernel wait objects (`WaitOnAddress` or futex), incurring context switch penalties (~1,000 to 3,000 CPU cycles).
   - In contrast, `std::atomic` uses hardware bus locking / cache coherence protocols (`LOCK XADD` on x86), remaining entirely in user space.

---

## License

MIT License. See `LICENSE` for details.

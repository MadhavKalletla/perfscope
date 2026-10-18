#include "perfscope/bench/benchmark_runner.hpp"
#include "perfscope/utils/string_utils.hpp"
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <mutex>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <cstring>

namespace perfscope {

CpuBenchResult BenchmarkRunner::run_cpu_benchmark(uint32_t duration_seconds) {
    const uint32_t num_threads = std::max(1u, std::thread::hardware_concurrency());
    std::atomic<bool> keep_running{true};
    std::vector<uint64_t> thread_ops(num_threads, 0);
    std::vector<std::thread> workers;
    workers.reserve(num_threads);

    const auto start_time = std::chrono::steady_clock::now();

    for (uint32_t t = 0; t < num_threads; ++t) {
        workers.emplace_back([t, &keep_running, &thread_ops]() {
            uint64_t ops = 0;
            volatile double acc = 1.000001;
            while (keep_running.load(std::memory_order_relaxed)) {
                // Compute-bound arithmetic work
                for (int i = 0; i < 1000; ++i) {
                    acc = acc * 1.0000001 + 0.0000001;
                }
                ops += 1000;
            }
            thread_ops[t] = ops;
        });
    }

    std::this_thread::sleep_for(std::chrono::seconds(duration_seconds));
    keep_running.store(false, std::memory_order_relaxed);

    for (auto& w : workers) {
        if (w.joinable()) w.join();
    }

    const auto end_time = std::chrono::steady_clock::now();
    const double elapsed_sec = std::chrono::duration<double>(end_time - start_time).count();

    uint64_t total_ops = 0;
    for (uint64_t ops : thread_ops) {
        total_ops += ops;
    }

    CpuBenchResult res;
    res.duration_seconds = elapsed_sec;
    res.total_operations = total_ops;
    res.thread_count = num_threads;
    res.ops_per_second = (elapsed_sec > 0.0) ? (static_cast<double>(total_ops) / elapsed_sec) : 0.0;
    return res;
}

MemoryBenchResult BenchmarkRunner::run_memory_benchmark(size_t target_mb, uint32_t duration_seconds) {
    if (target_mb == 0) target_mb = 100;
    const size_t total_bytes = target_mb * 1024 * 1024;
    constexpr size_t PAGE_SIZE = 4096;
    constexpr size_t CHUNK_SIZE = 5 * 1024 * 1024; // 5 MB chunks

    const size_t num_chunks = (total_bytes + CHUNK_SIZE - 1) / CHUNK_SIZE;
    const auto step_delay_ms = std::chrono::milliseconds(
        (duration_seconds > 0) ? (duration_seconds * 1000 / std::max(size_t(1), num_chunks)) : 10);

    std::vector<std::vector<char>> allocated_buffers;
    allocated_buffers.reserve(num_chunks);
    size_t total_pages_touched = 0;
    size_t actual_bytes = 0;

    const auto start_time = std::chrono::steady_clock::now();

    for (size_t c = 0; c < num_chunks; ++c) {
        const size_t current_chunk = std::min(CHUNK_SIZE, total_bytes - actual_bytes);
        if (current_chunk == 0) break;

        std::vector<char> buffer(current_chunk);
        // Force the OS to map physical page frames by touching each 4096-byte page
        for (size_t offset = 0; offset < current_chunk; offset += PAGE_SIZE) {
            buffer[offset] = static_cast<char>((offset / PAGE_SIZE) % 127);
            ++total_pages_touched;
        }

        actual_bytes += current_chunk;
        allocated_buffers.push_back(std::move(buffer));
        std::this_thread::sleep_for(step_delay_ms);
    }

    const auto end_time = std::chrono::steady_clock::now();
    const double elapsed_sec = std::chrono::duration<double>(end_time - start_time).count();

    MemoryBenchResult res;
    res.duration_seconds = elapsed_sec;
    res.target_allocation_bytes = total_bytes;
    res.actual_allocated_bytes = actual_bytes;
    res.pages_touched = total_pages_touched;
    const double allocated_mb = static_cast<double>(actual_bytes) / (1024.0 * 1024.0);
    res.allocation_rate_mb_per_sec = (elapsed_sec > 0.0) ? (allocated_mb / elapsed_sec) : 0.0;
    return res;
}

ContentionBenchResult BenchmarkRunner::run_contention_benchmark(uint32_t thread_count, uint64_t iterations) {
    if (thread_count == 0) {
        thread_count = std::max(2u, std::thread::hardware_concurrency());
    }
    if (iterations == 0) iterations = 5000000;

    const uint64_t iters_per_thread = iterations / thread_count;
    ContentionBenchResult res;
    res.thread_count = thread_count;
    res.total_iterations = iters_per_thread * thread_count;

    // --- Workload A: std::mutex Contention ---
    {
        std::mutex mtx;
        uint64_t shared_counter = 0;
        std::vector<std::thread> threads;
        threads.reserve(thread_count);

        const auto t0 = std::chrono::high_resolution_clock::now();
        for (uint32_t t = 0; t < thread_count; ++t) {
            threads.emplace_back([&mtx, &shared_counter, iters_per_thread]() {
                for (uint64_t i = 0; i < iters_per_thread; ++i) {
                    std::lock_guard<std::mutex> lock(mtx);
                    ++shared_counter;
                }
            });
        }
        for (auto& th : threads) th.join();
        const auto t1 = std::chrono::high_resolution_clock::now();

        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        res.mutex_duration_ms = ms;
        res.mutex_ops_per_sec = (ms > 0.0) ? (static_cast<double>(res.total_iterations) / (ms / 1000.0)) : 0.0;
    }

    // --- Workload B: std::atomic Contention ---
    {
        std::atomic<uint64_t> shared_atomic{0};
        std::vector<std::thread> threads;
        threads.reserve(thread_count);

        const auto t0 = std::chrono::high_resolution_clock::now();
        for (uint32_t t = 0; t < thread_count; ++t) {
            threads.emplace_back([&shared_atomic, iters_per_thread]() {
                for (uint64_t i = 0; i < iters_per_thread; ++i) {
                    shared_atomic.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }
        for (auto& th : threads) th.join();
        const auto t1 = std::chrono::high_resolution_clock::now();

        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        res.atomic_duration_ms = ms;
        res.atomic_ops_per_sec = (ms > 0.0) ? (static_cast<double>(res.total_iterations) / (ms / 1000.0)) : 0.0;
    }

    res.speedup_factor = (res.mutex_ops_per_sec > 0.0) 
        ? (res.atomic_ops_per_sec / res.mutex_ops_per_sec) : 1.0;

    return res;
}

FullBenchmarkReport BenchmarkRunner::run_all() {
    FullBenchmarkReport report;

    std::cout << "\n=======================================================\n"
              << "       perfscope System Performance Benchmark Suite    \n"
              << "=======================================================\n\n";

    // 1. CPU Benchmark
    std::cout << "[1/3] Running CPU-bound multi-threaded workload (3 seconds)...\n";
    report.cpu_result = run_cpu_benchmark(3);
    std::cout << "      Threads utilized:  " << report.cpu_result.thread_count << "\n"
              << "      Total operations:  " << report.cpu_result.total_operations << "\n"
              << "      Throughput:        " << std::fixed << std::setprecision(2)
              << (report.cpu_result.ops_per_second / 1e6) << " Million Ops/sec\n\n";

    // 2. Memory Growth Benchmark
    std::cout << "[2/3] Running Memory Allocation & Physical Page Touch workload (100 MB)...\n";
    report.mem_result = run_memory_benchmark(100, 2);
    std::cout << "      Allocated & Touched: " << StringUtils::format_bytes(report.mem_result.actual_allocated_bytes) << "\n"
              << "      4KB Pages faulted:   " << report.mem_result.pages_touched << " pages\n"
              << "      Allocation Rate:     " << std::fixed << std::setprecision(2)
              << report.mem_result.allocation_rate_mb_per_sec << " MB/sec\n\n";

    // 3. Mutex vs Atomic Contention Benchmark
    std::cout << "[3/3] Running Multi-threaded Lock Contention (Mutex vs Atomics)...\n";
    report.contention_result = run_contention_benchmark(0, 4000000);
    std::cout << "      Contending Threads:  " << report.contention_result.thread_count << "\n"
              << "      Total Operations:    " << report.contention_result.total_iterations << "\n"
              << "      std::mutex Duration: " << std::fixed << std::setprecision(2)
              << report.contention_result.mutex_duration_ms << " ms ("
              << (report.contention_result.mutex_ops_per_sec / 1e6) << " M ops/sec)\n"
              << "      std::atomic Duration:" << report.contention_result.atomic_duration_ms << " ms ("
              << (report.contention_result.atomic_ops_per_sec / 1e6) << " M ops/sec)\n"
              << "      Atomic Speedup:      " << std::fixed << std::setprecision(2)
              << report.contention_result.speedup_factor << "x faster than mutex\n\n"
              << "=======================================================\n"
              << "Benchmark completed successfully.\n";

    return report;
}

} // namespace perfscope

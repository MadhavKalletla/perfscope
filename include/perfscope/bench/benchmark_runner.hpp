#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace perfscope {

struct CpuBenchResult {
    double duration_seconds{0.0};
    uint64_t total_operations{0};
    double ops_per_second{0.0};
    uint32_t thread_count{0};
};

struct MemoryBenchResult {
    double duration_seconds{0.0};
    size_t target_allocation_bytes{0};
    size_t actual_allocated_bytes{0};
    size_t pages_touched{0};
    double allocation_rate_mb_per_sec{0.0};
};

struct ContentionBenchResult {
    uint32_t thread_count{0};
    uint64_t total_iterations{0};
    double mutex_duration_ms{0.0};
    double mutex_ops_per_sec{0.0};
    double atomic_duration_ms{0.0};
    double atomic_ops_per_sec{0.0};
    double speedup_factor{0.0}; // atomic_ops / mutex_ops
};

struct FullBenchmarkReport {
    CpuBenchResult cpu_result;
    MemoryBenchResult mem_result;
    ContentionBenchResult contention_result;
};

class BenchmarkRunner {
public:
    /**
     * @brief Executes the CPU-bound workload across all available cores.
     */
    static CpuBenchResult run_cpu_benchmark(uint32_t duration_seconds = 3);

    /**
     * @brief Executes incremental memory growth workload, page-faulting physical RAM.
     */
    static MemoryBenchResult run_memory_benchmark(size_t target_mb = 100, uint32_t duration_seconds = 3);

    /**
     * @brief Measures contention latency between std::mutex and std::atomic across N threads.
     */
    static ContentionBenchResult run_contention_benchmark(uint32_t thread_count = 0, uint64_t iterations = 5000000);

    /**
     * @brief Runs the complete benchmark suite and prints formatted report.
     */
    static FullBenchmarkReport run_all();
};

} // namespace perfscope

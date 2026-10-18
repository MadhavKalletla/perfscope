#pragma once

#include "perfscope/sampler/process_sampler.hpp"
#include <string>
#include <vector>

namespace perfscope {

enum class AnomalyType {
    CPU_SPIKE,
    MEMORY_LEAK_GROWTH,
    THREAD_GROWTH,
    HANDLE_LEAK
};

struct Anomaly {
    AnomalyType type;
    std::string title;
    std::string description;
    std::string rule_explanation;
    uint64_t timestamp_ms{0};
    double metric_value{0.0};
    double baseline_value{0.0};
};

struct AnomalyDetectorConfig {
    // CPU Spike rules:
    // 1. Sudden jump between consecutive samples >= 40.0%
    double cpu_jump_threshold{40.0};
    // 2. Sample exceeds mean + k * stddev with minimum threshold of 30.0%
    double cpu_sigma_multiplier{2.5};
    double cpu_min_spike_value{30.0};

    // Memory Leak rules:
    // Computes ordinary least squares (OLS) linear regression on private_bytes over time.
    // Flagged if slope > min_leak_bytes_per_sec AND R-squared correlation >= min_leak_r_squared.
    double min_leak_bytes_per_sec{50000.0}; // ~50 KB/sec sustained
    double min_leak_r_squared{0.75};       // Strong linear growth trend
    size_t min_samples_for_regression{5};  // Need at least 5 samples for trend
};

class AnomalyDetector {
public:
    explicit AnomalyDetector(AnomalyDetectorConfig config = AnomalyDetectorConfig())
        : config_(config) {}

    /**
     * @brief Evaluates a full series of ProcessSamples and returns all flagged anomalies.
     */
    std::vector<Anomaly> detect_anomalies(const std::vector<ProcessSample>& samples) const;

private:
    AnomalyDetectorConfig config_;

    std::vector<Anomaly> detect_cpu_spikes(const std::vector<ProcessSample>& samples) const;
    std::vector<Anomaly> detect_memory_growth(const std::vector<ProcessSample>& samples) const;
};

} // namespace perfscope

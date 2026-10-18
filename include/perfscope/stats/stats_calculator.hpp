#pragma once

#include <vector>
#include <string>
#include <cstddef>
#include <optional>

namespace perfscope {

/**
 * @brief Summary statistics for a single metric series.
 */
struct MetricSummary {
    size_t count{0};
    double min{0.0};
    double mean{0.0};
    double p50{0.0};
    double p95{0.0};
    double p99{0.0};
    double max{0.0};
    double stddev{0.0};
};

/**
 * @brief Complete statistical summary for a process trace.
 */
struct ProcessStatsSummary {
    size_t sample_count{0};
    double duration_seconds{0.0};
    MetricSummary cpu_percent;
    MetricSummary working_set_bytes;
    MetricSummary private_bytes;
    MetricSummary thread_count;
    MetricSummary handle_count;
};

class StatsCalculator {
public:
    /**
     * @brief Computes min, mean, p50, p95, p99, max, and stddev for a series of values.
     * 
     * Uses nearest-rank / linear interpolation for percentiles.
     * Handles edge cases gracefully: empty input returns std::nullopt.
     */
    static std::optional<MetricSummary> compute_summary(std::vector<double> values);

    /**
     * @brief Computes percentile (0.0 to 100.0) on a pre-sorted vector.
     * Uses linear interpolation between surrounding ranks.
     */
    static double compute_percentile(const std::vector<double>& sorted_values, double percentile);
};

} // namespace perfscope

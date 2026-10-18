#include "perfscope/stats/stats_calculator.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>

namespace perfscope {

double StatsCalculator::compute_percentile(const std::vector<double>& sorted_values, double percentile) {
    if (sorted_values.empty()) return 0.0;
    if (sorted_values.size() == 1) return sorted_values.front();
    if (percentile <= 0.0) return sorted_values.front();
    if (percentile >= 100.0) return sorted_values.back();

    // Standard linear interpolation between adjacent ranks (NIST / NumPy default)
    const double rank = (percentile / 100.0) * static_cast<double>(sorted_values.size() - 1);
    const size_t lower_idx = static_cast<size_t>(std::floor(rank));
    const size_t upper_idx = static_cast<size_t>(std::ceil(rank));
    const double weight = rank - static_cast<double>(lower_idx);

    if (lower_idx == upper_idx || upper_idx >= sorted_values.size()) {
        return sorted_values[lower_idx];
    }

    return sorted_values[lower_idx] * (1.0 - weight) + sorted_values[upper_idx] * weight;
}

std::optional<MetricSummary> StatsCalculator::compute_summary(std::vector<double> values) {
    if (values.empty()) {
        return std::nullopt;
    }

    MetricSummary summary;
    summary.count = values.size();

    // Sort ascending for min, max, and percentiles
    std::sort(values.begin(), values.end());

    summary.min = values.front();
    summary.max = values.back();

    const double sum = std::accumulate(values.begin(), values.end(), 0.0);
    summary.mean = sum / static_cast<double>(summary.count);

    // Compute standard deviation (sample standard deviation if count > 1, else 0.0)
    double var_sum = 0.0;
    for (double val : values) {
        const double diff = val - summary.mean;
        var_sum += diff * diff;
    }
    const double denom = (summary.count > 1) ? static_cast<double>(summary.count - 1) : 1.0;
    summary.stddev = std::sqrt(var_sum / denom);

    summary.p50 = compute_percentile(values, 50.0);
    summary.p95 = compute_percentile(values, 95.0);
    summary.p99 = compute_percentile(values, 99.0);

    return summary;
}

} // namespace perfscope

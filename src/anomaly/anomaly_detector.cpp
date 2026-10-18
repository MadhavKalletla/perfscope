#include "perfscope/anomaly/anomaly_detector.hpp"
#include "perfscope/stats/stats_calculator.hpp"
#include "perfscope/utils/string_utils.hpp"
#include <cmath>
#include <numeric>
#include <sstream>
#include <iomanip>

namespace perfscope {

std::vector<Anomaly> AnomalyDetector::detect_anomalies(const std::vector<ProcessSample>& samples) const {
    std::vector<Anomaly> results;
    if (samples.empty()) {
        return results;
    }

    auto spikes = detect_cpu_spikes(samples);
    results.insert(results.end(), spikes.begin(), spikes.end());

    auto leaks = detect_memory_growth(samples);
    results.insert(results.end(), leaks.begin(), leaks.end());

    return results;
}

std::vector<Anomaly> AnomalyDetector::detect_cpu_spikes(const std::vector<ProcessSample>& samples) const {
    std::vector<Anomaly> anomalies;
    if (samples.size() < 2) return anomalies;

    std::vector<double> cpu_vals;
    cpu_vals.reserve(samples.size());
    for (const auto& s : samples) {
        cpu_vals.push_back(s.cpu_percent);
    }

    const auto summary = StatsCalculator::compute_summary(cpu_vals);
    if (!summary) return anomalies;

    const double outlier_cutoff = summary->mean + (config_.cpu_sigma_multiplier * summary->stddev);

    for (size_t i = 1; i < samples.size(); ++i) {
        const double curr = samples[i].cpu_percent;
        const double prev = samples[i - 1].cpu_percent;
        const double delta = curr - prev;

        // Rule 1: Sudden Jump between consecutive samples
        if (delta >= config_.cpu_jump_threshold) {
            Anomaly a;
            a.type = AnomalyType::CPU_SPIKE;
            a.title = "Sudden CPU Spike";
            a.timestamp_ms = samples[i].timestamp_ms;
            a.metric_value = curr;
            a.baseline_value = prev;

            std::ostringstream desc;
            desc << std::fixed << std::setprecision(1)
                 << "CPU surged by +" << delta << "% (from " << prev << "% to " << curr << "%) at "
                 << samples[i].timestamp_iso;
            a.description = desc.str();

            std::ostringstream rule;
            rule << "Rule: Consecutive sample delta (" << delta << "%) exceeds threshold ("
                 << config_.cpu_jump_threshold << "%).";
            a.rule_explanation = rule.str();

            anomalies.push_back(a);
        }
        // Rule 2: Outlier exceeding Mean + k * Sigma (if not already triggered by Rule 1)
        else if (curr >= outlier_cutoff && curr >= config_.cpu_min_spike_value && summary->stddev > 2.0) {
            Anomaly a;
            a.type = AnomalyType::CPU_SPIKE;
            a.title = "Statistical CPU Outlier";
            a.timestamp_ms = samples[i].timestamp_ms;
            a.metric_value = curr;
            a.baseline_value = summary->mean;

            std::ostringstream desc;
            desc << std::fixed << std::setprecision(1)
                 << "CPU reached " << curr << "% (baseline mean: " << summary->mean 
                 << "%, stddev: " << summary->stddev << "%) at " << samples[i].timestamp_iso;
            a.description = desc.str();

            std::ostringstream rule;
            rule << "Rule: Value exceeds mean + " << config_.cpu_sigma_multiplier << "*sigma ("
                 << outlier_cutoff << "%).";
            a.rule_explanation = rule.str();

            anomalies.push_back(a);
        }
    }

    return anomalies;
}

std::vector<Anomaly> AnomalyDetector::detect_memory_growth(const std::vector<ProcessSample>& samples) const {
    std::vector<Anomaly> anomalies;
    if (samples.size() < config_.min_samples_for_regression) {
        return anomalies;
    }

    // Use Ordinary Least Squares (OLS) linear regression on private_bytes over time
    const uint64_t start_time = samples.front().timestamp_ms;
    std::vector<double> t_sec;
    std::vector<double> bytes;
    t_sec.reserve(samples.size());
    bytes.reserve(samples.size());

    for (const auto& s : samples) {
        const double sec = static_cast<double>(s.timestamp_ms - start_time) / 1000.0;
        t_sec.push_back(sec);
        bytes.push_back(static_cast<double>(s.private_bytes));
    }

    const double n = static_cast<double>(samples.size());
    const double sum_t = std::accumulate(t_sec.begin(), t_sec.end(), 0.0);
    const double sum_y = std::accumulate(bytes.begin(), bytes.end(), 0.0);
    const double mean_t = sum_t / n;
    const double mean_y = sum_y / n;

    double num = 0.0;
    double denom_t = 0.0;
    double denom_y = 0.0;

    for (size_t i = 0; i < samples.size(); ++i) {
        const double diff_t = t_sec[i] - mean_t;
        const double diff_y = bytes[i] - mean_y;
        num += diff_t * diff_y;
        denom_t += diff_t * diff_t;
        denom_y += diff_y * diff_y;
    }

    if (denom_t <= 0.0 || denom_y <= 0.0) {
        return anomalies; // No variance in time or memory
    }

    const double slope = num / denom_t; // Bytes per second
    const double r = num / std::sqrt(denom_t * denom_y);
    const double r_squared = r * r;

    // Check if slope is positive, exceeds threshold, and exhibits strong linear correlation
    if (slope >= config_.min_leak_bytes_per_sec && r_squared >= config_.min_leak_r_squared) {
        const double rate_mb_per_min = (slope * 60.0) / (1024.0 * 1024.0);
        const uint64_t total_net_growth = (samples.back().private_bytes >= samples.front().private_bytes)
            ? (samples.back().private_bytes - samples.front().private_bytes) : 0;

        Anomaly a;
        a.type = AnomalyType::MEMORY_LEAK_GROWTH;
        a.title = "Steady Memory Growth (Suspected Leak)";
        a.timestamp_ms = samples.back().timestamp_ms;
        a.metric_value = static_cast<double>(samples.back().private_bytes);
        a.baseline_value = static_cast<double>(samples.front().private_bytes);

        std::ostringstream desc;
        desc << std::fixed << std::setprecision(2)
             << "Private memory grew by " << StringUtils::format_bytes(total_net_growth)
             << " at an estimated rate of +" << rate_mb_per_min << " MB/min (from "
             << StringUtils::format_bytes(samples.front().private_bytes) << " to "
             << StringUtils::format_bytes(samples.back().private_bytes) << ").";
        a.description = desc.str();

        std::ostringstream rule;
        rule << "Rule: OLS Linear Regression slope (" << StringUtils::format_bytes(static_cast<uint64_t>(slope))
             << "/s) >= " << StringUtils::format_bytes(static_cast<uint64_t>(config_.min_leak_bytes_per_sec))
             << "/s AND goodness of fit R² (" << std::fixed << std::setprecision(3) << r_squared
             << ") >= " << config_.min_leak_r_squared << ".";
        a.rule_explanation = rule.str();

        anomalies.push_back(a);
    }

    return anomalies;
}

} // namespace perfscope

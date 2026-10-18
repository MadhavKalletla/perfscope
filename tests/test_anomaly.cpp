#include "catch2/catch.hpp"
#include "perfscope/anomaly/anomaly_detector.hpp"

using namespace perfscope;

TEST_CASE("AnomalyDetector flags sudden CPU spike", "[anomaly]") {
    AnomalyDetector detector;
    std::vector<ProcessSample> samples;

    // Normal samples around 5% CPU
    for (uint64_t i = 0; i < 5; ++i) {
        ProcessSample s;
        s.timestamp_ms = 1000 + i * 500;
        s.timestamp_iso = "2026-10-18T12:00:0" + std::to_string(i) + ".000Z";
        s.cpu_percent = 5.0 + (i % 2);
        s.private_bytes = 10000000;
        samples.push_back(s);
    }

    // Sudden surge to 75%
    ProcessSample spike;
    spike.timestamp_ms = 3500;
    spike.timestamp_iso = "2026-10-18T12:00:05.000Z";
    spike.cpu_percent = 75.0;
    spike.private_bytes = 10000000;
    samples.push_back(spike);

    auto anomalies = detector.detect_anomalies(samples);
    REQUIRE_FALSE(anomalies.empty());

    bool found_cpu_spike = false;
    for (const auto& a : anomalies) {
        if (a.type == AnomalyType::CPU_SPIKE) {
            found_cpu_spike = true;
            CHECK(a.metric_value == Approx(75.0));
        }
    }
    CHECK(found_cpu_spike);
}

TEST_CASE("AnomalyDetector flags steady memory growth suggesting a leak", "[anomaly]") {
    AnomalyDetector detector;
    std::vector<ProcessSample> samples;

    // 10 samples with strictly increasing private memory: growing 200 KB per 500ms (~400 KB/s)
    uint64_t current_mem = 20 * 1024 * 1024; // 20 MB
    for (uint64_t i = 0; i < 10; ++i) {
        ProcessSample s;
        s.timestamp_ms = 1000 + i * 500;
        s.timestamp_iso = "2026-10-18T12:00:0" + std::to_string(i) + ".000Z";
        s.cpu_percent = 10.0;
        s.private_bytes = current_mem;
        current_mem += 200 * 1024; // +200 KB
        samples.push_back(s);
    }

    auto anomalies = detector.detect_anomalies(samples);
    REQUIRE_FALSE(anomalies.empty());

    bool found_mem_leak = false;
    for (const auto& a : anomalies) {
        if (a.type == AnomalyType::MEMORY_LEAK_GROWTH) {
            found_mem_leak = true;
            CHECK(a.metric_value > a.baseline_value);
        }
    }
    CHECK(found_mem_leak);
}

TEST_CASE("AnomalyDetector does not flag stable or fluctuating memory", "[anomaly]") {
    AnomalyDetector detector;
    std::vector<ProcessSample> samples;

    // 10 samples fluctuating around 30 MB (+- 50 KB)
    for (uint64_t i = 0; i < 10; ++i) {
        ProcessSample s;
        s.timestamp_ms = 1000 + i * 500;
        s.timestamp_iso = "2026-10-18T12:00:0" + std::to_string(i) + ".000Z";
        s.cpu_percent = 5.0;
        // Periodic fluctuation
        s.private_bytes = 30000000 + ((i % 2 == 0) ? 50000 : -50000);
        samples.push_back(s);
    }

    auto anomalies = detector.detect_anomalies(samples);
    for (const auto& a : anomalies) {
        CHECK(a.type != AnomalyType::MEMORY_LEAK_GROWTH);
    }
}

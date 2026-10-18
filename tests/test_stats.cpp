#include "catch2/catch.hpp"
#include "perfscope/stats/stats_calculator.hpp"
#include <vector>
#include <numeric>

using namespace perfscope;

TEST_CASE("StatsCalculator handles empty input gracefully", "[stats]") {
    std::vector<double> empty_vec;
    auto summary = StatsCalculator::compute_summary(empty_vec);
    REQUIRE_FALSE(summary.has_value());
}

TEST_CASE("StatsCalculator handles single-element input", "[stats]") {
    std::vector<double> single_vec = {42.5};
    auto summary = StatsCalculator::compute_summary(single_vec);
    REQUIRE(summary.has_value());
    CHECK(summary->count == 1);
    CHECK(summary->min == Approx(42.5));
    CHECK(summary->mean == Approx(42.5));
    CHECK(summary->p50 == Approx(42.5));
    CHECK(summary->p95 == Approx(42.5));
    CHECK(summary->p99 == Approx(42.5));
    CHECK(summary->max == Approx(42.5));
    CHECK(summary->stddev == Approx(0.0));
}

TEST_CASE("StatsCalculator handles identical values", "[stats]") {
    std::vector<double> identical_vec(10, 100.0);
    auto summary = StatsCalculator::compute_summary(identical_vec);
    REQUIRE(summary.has_value());
    CHECK(summary->count == 10);
    CHECK(summary->min == Approx(100.0));
    CHECK(summary->mean == Approx(100.0));
    CHECK(summary->p50 == Approx(100.0));
    CHECK(summary->p95 == Approx(100.0));
    CHECK(summary->p99 == Approx(100.0));
    CHECK(summary->max == Approx(100.0));
    CHECK(summary->stddev == Approx(0.0).margin(1e-6));
}

TEST_CASE("StatsCalculator calculates correct percentiles and moments on known sequence", "[stats]") {
    // Sequence 1.0, 2.0, ..., 100.0
    std::vector<double> seq(100);
    std::iota(seq.begin(), seq.end(), 1.0);

    auto summary = StatsCalculator::compute_summary(seq);
    REQUIRE(summary.has_value());
    CHECK(summary->count == 100);
    CHECK(summary->min == Approx(1.0));
    CHECK(summary->max == Approx(100.0));
    CHECK(summary->mean == Approx(50.5));
    CHECK(summary->p50 == Approx(50.5));
    CHECK(summary->p95 == Approx(95.05));
    CHECK(summary->p99 == Approx(99.01));
    CHECK(summary->stddev == Approx(29.01149).epsilon(0.01));
}

TEST_CASE("StatsCalculator percentile interpolation edge cases", "[stats]") {
    std::vector<double> sorted = {10.0, 20.0, 30.0, 40.0, 50.0};
    CHECK(StatsCalculator::compute_percentile(sorted, 0.0) == Approx(10.0));
    CHECK(StatsCalculator::compute_percentile(sorted, 100.0) == Approx(50.0));
    CHECK(StatsCalculator::compute_percentile(sorted, 50.0) == Approx(30.0));
    CHECK(StatsCalculator::compute_percentile(sorted, 25.0) == Approx(20.0));
}

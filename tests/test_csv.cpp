#include "catch2/catch.hpp"
#include "perfscope/csv/csv_io.hpp"
#include <sstream>

using namespace perfscope;

TEST_CASE("CsvReader rejects empty stream", "[csv]") {
    std::istringstream iss("");
    auto res = CsvReader::parse(iss);
    CHECK_FALSE(res.success);
    CHECK(res.samples.empty());
}

TEST_CASE("CsvReader parses valid samples with mixed formatting", "[csv]") {
    std::string csv_data = 
        "timestamp_iso,timestamp_ms,pid,cpu_percent,working_set_bytes,private_bytes,thread_count,handle_count\n"
        "2026-10-18T12:00:00.000Z, 1729252800000, 1024, 15.5, 52428800, 41943040, 8, 120\n"
        "2026-10-18T12:00:00.500Z, 1729252800500, 1024, 25.0, 54525952, 44040192, 9, 124\n";

    std::istringstream iss(csv_data);
    auto res = CsvReader::parse(iss);

    REQUIRE(res.success);
    REQUIRE(res.samples.size() == 2);
    CHECK(res.total_rows_read == 2);
    CHECK(res.malformed_rows == 0);

    CHECK(res.samples[0].pid == 1024);
    CHECK(res.samples[0].cpu_percent == Approx(15.5));
    CHECK(res.samples[0].working_set_bytes == 52428800);
    CHECK(res.samples[0].private_bytes == 41943040);
    CHECK(res.samples[0].thread_count == 8);
    CHECK(res.samples[0].handle_count == 120);

    CHECK(res.samples[1].cpu_percent == Approx(25.0));
    CHECK(res.samples[1].thread_count == 9);
}

TEST_CASE("CsvReader skips malformed rows without crashing", "[csv]") {
    std::string csv_data = 
        "timestamp_iso,timestamp_ms,pid,cpu_percent,working_set_bytes,private_bytes,thread_count,handle_count\n"
        "2026-10-18T12:00:00.000Z, 1729252800000, 1024, 15.5, 52428800, 41943040, 8, 120\n"
        "MALFORMED_ROW_FEW_COLUMNS, 12345\n"
        "2026-10-18T12:00:01.000Z, not_a_number, 1024, not_a_float, 52428800, 41943040, 8, 120\n"
        "2026-10-18T12:00:01.500Z, 1729252801500, 1024, 30.2, 60000000, 50000000, 10, 130\n";

    std::istringstream iss(csv_data);
    auto res = CsvReader::parse(iss);

    REQUIRE(res.success);
    REQUIRE(res.samples.size() == 2); // Only rows 1 and 4 are valid
    CHECK(res.total_rows_read == 4);
    CHECK(res.malformed_rows == 2);
    CHECK(res.samples[1].cpu_percent == Approx(30.2));
}

TEST_CASE("CsvReader handles header-only CSV", "[csv]") {
    std::string csv_data = 
        "timestamp_iso,timestamp_ms,pid,cpu_percent,working_set_bytes,private_bytes,thread_count,handle_count\n";

    std::istringstream iss(csv_data);
    auto res = CsvReader::parse(iss);

    REQUIRE(res.success);
    CHECK(res.samples.empty());
    CHECK(res.total_rows_read == 0);
}

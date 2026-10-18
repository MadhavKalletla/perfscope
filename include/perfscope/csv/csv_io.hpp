#pragma once

#include "perfscope/sampler/process_sampler.hpp"
#include <string>
#include <vector>
#include <iostream>
#include <fstream>

namespace perfscope {

class CsvWriter {
public:
    explicit CsvWriter(const std::string& filepath);
    ~CsvWriter();

    // Writes the CSV header row
    bool write_header();

    // Appends a single sample row to the CSV
    bool write_sample(const ProcessSample& sample);

    // Flushes buffer to disk
    void flush();

    // Closes file stream
    void close();

    bool is_open() const;

private:
    std::string filepath_;
    std::ofstream stream_;
};

struct CsvParseResult {
    bool success{false};
    std::string error_message;
    std::vector<ProcessSample> samples;
    size_t total_rows_read{0};
    size_t malformed_rows{0};
};

class CsvReader {
public:
    /**
     * @brief Parses a perfscope-compatible CSV file.
     * Robust against whitespace, malformed rows, and missing values.
     */
    static CsvParseResult read_csv(const std::string& filepath);

    /**
     * @brief Parses CSV content from an input stream.
     */
    static CsvParseResult parse(std::istream& is);
};

} // namespace perfscope

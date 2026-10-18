#include "perfscope/csv/csv_io.hpp"
#include "perfscope/utils/string_utils.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace perfscope {

CsvWriter::CsvWriter(const std::string& filepath)
    : filepath_(filepath), stream_(filepath, std::ios::out | std::ios::trunc) {}

CsvWriter::~CsvWriter() {
    close();
}

bool CsvWriter::is_open() const {
    return stream_.is_open();
}

bool CsvWriter::write_header() {
    if (!is_open()) return false;
    stream_ << "timestamp_iso,timestamp_ms,pid,cpu_percent,working_set_bytes,private_bytes,thread_count,handle_count\n";
    stream_.flush();
    return stream_.good();
}

bool CsvWriter::write_sample(const ProcessSample& sample) {
    if (!is_open()) return false;
    stream_ << sample.timestamp_iso << ","
            << sample.timestamp_ms << ","
            << sample.pid << ","
            << std::fixed << std::setprecision(2) << sample.cpu_percent << ","
            << sample.working_set_bytes << ","
            << sample.private_bytes << ","
            << sample.thread_count << ","
            << sample.handle_count << "\n";
    return stream_.good();
}

void CsvWriter::flush() {
    if (is_open()) {
        stream_.flush();
    }
}

void CsvWriter::close() {
    if (is_open()) {
        stream_.flush();
        stream_.close();
    }
}

// Helper: split a CSV line by comma, handling potential quotes
static std::vector<std::string> split_csv_line(const std::string& line) {
    std::vector<std::string> tokens;
    std::string current;
    bool in_quotes = false;

    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '"') {
            in_quotes = !in_quotes;
        } else if (c == ',' && !in_quotes) {
            tokens.push_back(StringUtils::trim(current));
            current.clear();
        } else {
            current += c;
        }
    }
    tokens.push_back(StringUtils::trim(current));
    return tokens;
}

CsvParseResult CsvReader::read_csv(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        CsvParseResult res;
        res.success = false;
        res.error_message = "Unable to open file: " + filepath;
        return res;
    }
    return parse(file);
}

CsvParseResult CsvReader::parse(std::istream& is) {
    CsvParseResult res;
    std::string line;

    // Read header line
    while (std::getline(is, line)) {
        line = StringUtils::trim(line);
        if (!line.empty()) break;
    }

    if (line.empty()) {
        res.success = false;
        res.error_message = "CSV input is empty.";
        return res;
    }

    const auto header_cols = split_csv_line(line);
    // Find column indices
    int idx_iso = -1, idx_ms = -1, idx_pid = -1, idx_cpu = -1;
    int idx_ws = -1, idx_priv = -1, idx_threads = -1, idx_handles = -1;

    for (size_t i = 0; i < header_cols.size(); ++i) {
        std::string col = header_cols[i];
        std::transform(col.begin(), col.end(), col.begin(), [](unsigned char c){ return std::tolower(c); });
        
        if (col == "timestamp_iso" || col == "timestamp" || col == "time") idx_iso = static_cast<int>(i);
        else if (col == "timestamp_ms" || col == "epoch_ms") idx_ms = static_cast<int>(i);
        else if (col == "pid") idx_pid = static_cast<int>(i);
        else if (col == "cpu_percent" || col == "cpu" || col == "cpu_%") idx_cpu = static_cast<int>(i);
        else if (col == "working_set_bytes" || col == "working_set" || col == "ws_bytes") idx_ws = static_cast<int>(i);
        else if (col == "private_bytes" || col == "private_usage" || col == "priv_bytes") idx_priv = static_cast<int>(i);
        else if (col == "thread_count" || col == "threads") idx_threads = static_cast<int>(i);
        else if (col == "handle_count" || col == "handles") idx_handles = static_cast<int>(i);
    }

    // At least CPU or memory must be present
    if (idx_cpu == -1 && idx_ws == -1) {
        res.success = false;
        res.error_message = "CSV header missing required metric columns (cpu_percent, working_set_bytes).";
        return res;
    }

    while (std::getline(is, line)) {
        line = StringUtils::trim(line);
        if (line.empty()) continue; // Skip empty rows

        ++res.total_rows_read;
        const auto tokens = split_csv_line(line);
        if (tokens.size() < header_cols.size()) {
            ++res.malformed_rows;
            continue;
        }

        try {
            ProcessSample sample;
            if (idx_iso >= 0 && idx_iso < static_cast<int>(tokens.size())) {
                sample.timestamp_iso = tokens[idx_iso];
            }
            if (idx_ms >= 0 && idx_ms < static_cast<int>(tokens.size())) {
                sample.timestamp_ms = std::stoull(tokens[idx_ms]);
            }
            if (idx_pid >= 0 && idx_pid < static_cast<int>(tokens.size())) {
                sample.pid = static_cast<uint32_t>(std::stoul(tokens[idx_pid]));
            }
            if (idx_cpu >= 0 && idx_cpu < static_cast<int>(tokens.size())) {
                sample.cpu_percent = std::stod(tokens[idx_cpu]);
            }
            if (idx_ws >= 0 && idx_ws < static_cast<int>(tokens.size())) {
                sample.working_set_bytes = std::stoull(tokens[idx_ws]);
            }
            if (idx_priv >= 0 && idx_priv < static_cast<int>(tokens.size())) {
                sample.private_bytes = std::stoull(tokens[idx_priv]);
            }
            if (idx_threads >= 0 && idx_threads < static_cast<int>(tokens.size())) {
                sample.thread_count = static_cast<uint32_t>(std::stoul(tokens[idx_threads]));
            }
            if (idx_handles >= 0 && idx_handles < static_cast<int>(tokens.size())) {
                sample.handle_count = static_cast<uint32_t>(std::stoul(tokens[idx_handles]));
            }

            res.samples.push_back(sample);
        } catch (...) {
            ++res.malformed_rows;
        }
    }

    res.success = true;
    return res;
}

} // namespace perfscope

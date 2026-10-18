#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace perfscope {

enum class CommandMode {
    NONE,
    WATCH,
    TOP,
    ANALYZE,
    BENCH,
    HELP,
    VERSION
};

struct CliOptions {
    CommandMode mode{CommandMode::NONE};
    
    // watch options
    uint32_t pid{0};
    bool pid_specified{false};
    uint32_t interval_ms{500};
    uint32_t duration_sec{30};
    bool duration_specified{false};
    std::string output_file; // Defaults to perfscope_pid_<pid>.csv if empty

    // top options
    uint32_t top_limit{15};
    std::string top_sort{"cpu"}; // "cpu" or "mem"

    // analyze options
    std::string analyze_file;

    // bench options
    uint32_t bench_duration_sec{3};

    // General options
    bool show_help{false};
    bool show_version{false};
    std::string error_message;
};

class CliParser {
public:
    static CliOptions parse(int argc, char* argv[]);
    static void print_help();
    static void print_version();
};

} // namespace perfscope

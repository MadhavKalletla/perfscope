#include "perfscope/cli/cli_parser.hpp"
#include <iostream>
#include <string>
#include <sstream>

namespace perfscope {

void CliParser::print_help() {
    std::cout << "perfscope - Windows/Linux Process and System Performance Profiler\n"
              << "Version 1.0.0\n\n"
              << "USAGE:\n"
              << "  perfscope <command> [options]\n\n"
              << "COMMANDS:\n"
              << "  watch      Sample a target process and output metrics to CSV\n"
              << "  top        Display real-time snapshot of top processes by CPU or Memory\n"
              << "  analyze    Compute statistical summaries and detect performance anomalies\n"
              << "  bench      Run built-in reproducible performance benchmarks\n\n"
              << "WATCH OPTIONS:\n"
              << "  --pid <pid>           Target process ID to monitor (required)\n"
              << "  --interval <ms>       Sampling interval in milliseconds (default: 500)\n"
              << "  --duration <sec>      Total monitoring duration in seconds (default: 30)\n"
              << "  --output <file>       Path to output CSV file (default: perfscope_pid_<pid>.csv)\n\n"
              << "TOP OPTIONS:\n"
              << "  --limit <n>           Number of top processes to display (default: 15)\n"
              << "  --sort <cpu|mem>      Sort by 'cpu' percentage or 'mem' working set (default: cpu)\n\n"
              << "ANALYZE OPTIONS:\n"
              << "  <csv_file>            Path to the perfscope CSV file to analyze\n\n"
              << "BENCH OPTIONS:\n"
              << "  --duration <sec>      Duration in seconds for workload runs (default: 3)\n\n"
              << "GENERAL OPTIONS:\n"
              << "  -h, --help            Show this help documentation\n"
              << "  -v, --version         Show version information\n\n"
              << "EXAMPLES:\n"
              << "  perfscope watch --pid 1234 --interval 500 --duration 30\n"
              << "  perfscope top --limit 10 --sort mem\n"
              << "  perfscope analyze trace.csv\n"
              << "  perfscope bench\n";
}

void CliParser::print_version() {
    std::cout << "perfscope version 1.0.0 (C++17, Windows/Linux)\n";
}

CliOptions CliParser::parse(int argc, char* argv[]) {
    CliOptions opt;
    if (argc < 2) {
        opt.show_help = true;
        return opt;
    }

    std::string command = argv[1];
    if (command == "-h" || command == "--help") {
        opt.show_help = true;
        return opt;
    }
    if (command == "-v" || command == "--version") {
        opt.show_version = true;
        return opt;
    }

    if (command == "watch") {
        opt.mode = CommandMode::WATCH;
        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--pid" && i + 1 < argc) {
                try {
                    opt.pid = static_cast<uint32_t>(std::stoul(argv[++i]));
                    opt.pid_specified = true;
                } catch (...) {
                    opt.error_message = "Invalid PID argument: " + std::string(argv[i]);
                    return opt;
                }
            } else if (arg == "--interval" && i + 1 < argc) {
                try {
                    opt.interval_ms = static_cast<uint32_t>(std::stoul(argv[++i]));
                    if (opt.interval_ms == 0) opt.interval_ms = 100;
                } catch (...) {
                    opt.error_message = "Invalid --interval argument: " + std::string(argv[i]);
                    return opt;
                }
            } else if (arg == "--duration" && i + 1 < argc) {
                try {
                    opt.duration_sec = static_cast<uint32_t>(std::stoul(argv[++i]));
                    opt.duration_specified = true;
                } catch (...) {
                    opt.error_message = "Invalid --duration argument: " + std::string(argv[i]);
                    return opt;
                }
            } else if (arg == "--output" && i + 1 < argc) {
                opt.output_file = argv[++i];
            } else if (arg == "-h" || arg == "--help") {
                opt.show_help = true;
                return opt;
            } else {
                opt.error_message = "Unknown option for watch: " + arg;
                return opt;
            }
        }

        if (!opt.pid_specified) {
            opt.error_message = "Missing required argument: --pid <pid>";
            return opt;
        }

        if (opt.output_file.empty()) {
            opt.output_file = "perfscope_pid_" + std::to_string(opt.pid) + ".csv";
        }

    } else if (command == "top") {
        opt.mode = CommandMode::TOP;
        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--limit" && i + 1 < argc) {
                try {
                    opt.top_limit = static_cast<uint32_t>(std::stoul(argv[++i]));
                } catch (...) {
                    opt.error_message = "Invalid --limit argument: " + std::string(argv[i]);
                    return opt;
                }
            } else if (arg == "--sort" && i + 1 < argc) {
                std::string sort_val = argv[++i];
                if (sort_val != "cpu" && sort_val != "mem") {
                    opt.error_message = "Invalid --sort argument (must be 'cpu' or 'mem'): " + sort_val;
                    return opt;
                }
                opt.top_sort = sort_val;
            } else if (arg == "-h" || arg == "--help") {
                opt.show_help = true;
                return opt;
            } else {
                opt.error_message = "Unknown option for top: " + arg;
                return opt;
            }
        }

    } else if (command == "analyze") {
        opt.mode = CommandMode::ANALYZE;
        if (argc < 3) {
            opt.error_message = "Missing CSV file path for analyze command. Usage: perfscope analyze <csv_file>";
            return opt;
        }
        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "-h" || arg == "--help") {
                opt.show_help = true;
                return opt;
            } else if (opt.analyze_file.empty()) {
                opt.analyze_file = arg;
            } else {
                opt.error_message = "Unexpected argument for analyze: " + arg;
                return opt;
            }
        }

    } else if (command == "bench") {
        opt.mode = CommandMode::BENCH;
        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--duration" && i + 1 < argc) {
                try {
                    opt.bench_duration_sec = static_cast<uint32_t>(std::stoul(argv[++i]));
                } catch (...) {
                    opt.error_message = "Invalid --duration argument: " + std::string(argv[i]);
                    return opt;
                }
            } else if (arg == "-h" || arg == "--help") {
                opt.show_help = true;
                return opt;
            } else {
                opt.error_message = "Unknown option for bench: " + arg;
                return opt;
            }
        }

    } else {
        opt.error_message = "Unknown command: '" + command + "'. Use --help for usage information.";
    }

    return opt;
}

} // namespace perfscope

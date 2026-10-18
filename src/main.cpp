#include "perfscope/cli/cli_parser.hpp"
#include "perfscope/sampler/sampler_factory.hpp"
#include "perfscope/csv/csv_io.hpp"
#include "perfscope/stats/stats_calculator.hpp"
#include "perfscope/anomaly/anomaly_detector.hpp"
#include "perfscope/bench/benchmark_runner.hpp"
#include "perfscope/utils/string_utils.hpp"
#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>
#include <algorithm>
#include <unordered_map>

namespace {
std::atomic<bool> g_stop_requested{false};

void signal_handler(int) {
    g_stop_requested.store(true);
}
}

namespace perfscope {

static int handle_watch(const CliOptions& opt) {
    auto sampler = SamplerFactory::create_sampler();

    if (!sampler->is_process_running(opt.pid)) {
        std::cerr << "Error: Process with PID " << opt.pid << " is not running or accessible.\n";
        return 1;
    }

    const std::string proc_name = sampler->get_process_name(opt.pid);
    std::cout << "Target: " << proc_name << " (PID: " << opt.pid << ")\n"
              << "Sampling interval: " << opt.interval_ms << " ms | Duration: "
              << opt.duration_sec << " s\n"
              << "Output CSV: " << opt.output_file << "\n\n";

    CsvWriter writer(opt.output_file);
    if (!writer.is_open()) {
        std::cerr << "Error: Unable to open output CSV file: " << opt.output_file << "\n";
        return 1;
    }

    if (!writer.write_header()) {
        std::cerr << "Error: Failed to write CSV header.\n";
        return 1;
    }

    // Set signal handlers for graceful exit on Ctrl+C
    std::signal(SIGINT, signal_handler);
#if !defined(_WIN32)
    std::signal(SIGTERM, signal_handler);
#endif

    std::cout << std::left << std::setw(25) << "Timestamp (ISO)"
              << std::right << std::setw(10) << "CPU %"
              << std::setw(16) << "Working Set"
              << std::setw(16) << "Private Bytes"
              << std::setw(10) << "Threads"
              << std::setw(10) << "Handles" << "\n";
    std::cout << std::string(87, '-') << "\n";

    const auto start_time = std::chrono::steady_clock::now();
    size_t samples_captured = 0;

    while (!g_stop_requested.load()) {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed_sec = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
        if (opt.duration_sec > 0 && elapsed_sec >= opt.duration_sec) {
            break;
        }

        ProcessSample sample;
        std::string err;
        if (!sampler->sample_process(opt.pid, sample, err)) {
            std::cerr << "\nSampling stopped: " << err << "\n";
            break;
        }

        writer.write_sample(sample);
        writer.flush();
        ++samples_captured;

        std::cout << std::left << std::setw(25) << sample.timestamp_iso
                  << std::right << std::fixed << std::setprecision(1) << std::setw(9) << sample.cpu_percent << "%"
                  << std::setw(16) << StringUtils::format_bytes(sample.working_set_bytes)
                  << std::setw(16) << StringUtils::format_bytes(sample.private_bytes)
                  << std::setw(10) << sample.thread_count
                  << std::setw(10) << sample.handle_count << "\n";

        std::this_thread::sleep_for(std::chrono::milliseconds(opt.interval_ms));
    }

    writer.close();
    std::cout << "\nFinished sampling: " << samples_captured << " samples saved to " << opt.output_file << "\n";
    return 0;
}

static int handle_top(const CliOptions& opt) {
    auto sampler = SamplerFactory::create_sampler();

    auto processes = sampler->list_processes();
    if (processes.empty()) {
        std::cerr << "No processes found or insufficient privileges to inspect processes.\n";
        return 1;
    }

    if (opt.top_sort == "mem") {
        // Memory metrics are already captured by list_processes()!
        std::sort(processes.begin(), processes.end(), [](const ProcessInfo& a, const ProcessInfo& b) {
            return a.working_set_bytes > b.working_set_bytes;
        });

        // Sample CPU delta only for top candidate processes
        const size_t candidates = std::min(static_cast<size_t>(opt.top_limit * 2), processes.size());
        for (size_t i = 0; i < candidates; ++i) {
            ProcessSample s;
            std::string err;
            sampler->sample_process(processes[i].pid, s, err);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        for (size_t i = 0; i < candidates; ++i) {
            ProcessSample s;
            std::string err;
            if (sampler->sample_process(processes[i].pid, s, err)) {
                processes[i].cpu_percent = s.cpu_percent;
            }
        }
    } else {
        // For CPU sort: sample candidate processes with active threads
        std::vector<size_t> candidate_indices;
        for (size_t i = 0; i < processes.size(); ++i) {
            if (processes[i].pid > 0 && processes[i].working_set_bytes > 0) {
                ProcessSample s;
                std::string err;
                if (sampler->sample_process(processes[i].pid, s, err)) {
                    candidate_indices.push_back(i);
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(250));

        for (size_t idx : candidate_indices) {
            ProcessSample s;
            std::string err;
            if (sampler->sample_process(processes[idx].pid, s, err)) {
                processes[idx].cpu_percent = s.cpu_percent;
                processes[idx].working_set_bytes = s.working_set_bytes;
                processes[idx].private_bytes = s.private_bytes;
                processes[idx].handle_count = s.handle_count;
            }
        }

        std::sort(processes.begin(), processes.end(), [](const ProcessInfo& a, const ProcessInfo& b) {
            if (a.cpu_percent != b.cpu_percent) {
                return a.cpu_percent > b.cpu_percent;
            }
            return a.working_set_bytes > b.working_set_bytes;
        });
    }

    std::cout << "\n" << std::left
              << std::setw(8) << "PID"
              << std::setw(28) << "Process Name"
              << std::right
              << std::setw(10) << "CPU %"
              << std::setw(16) << "Working Set"
              << std::setw(16) << "Private Bytes"
              << std::setw(10) << "Threads"
              << std::setw(10) << "Handles" << "\n";
    std::cout << std::string(98, '=') << "\n";

    const size_t limit = std::min(static_cast<size_t>(opt.top_limit), processes.size());
    for (size_t i = 0; i < limit; ++i) {
        const auto& p = processes[i];
        std::string short_name = p.name;
        if (short_name.size() > 26) {
            short_name = short_name.substr(0, 23) + "...";
        }

        std::cout << std::left
                  << std::setw(8) << p.pid
                  << std::setw(28) << short_name
                  << std::right << std::fixed << std::setprecision(1)
                  << std::setw(9) << p.cpu_percent << "%"
                  << std::setw(16) << StringUtils::format_bytes(p.working_set_bytes)
                  << std::setw(16) << StringUtils::format_bytes(p.private_bytes)
                  << std::setw(10) << p.thread_count
                  << std::setw(10) << p.handle_count << "\n";
    }

    std::cout << "\nShowing top " << limit << " of " << processes.size() << " processes (sorted by "
              << opt.top_sort << ").\n";
    return 0;
}

static int handle_analyze(const CliOptions& opt) {
    std::cout << "Loading CSV file: " << opt.analyze_file << "\n";
    const auto parse_res = CsvReader::read_csv(opt.analyze_file);

    if (!parse_res.success) {
        std::cerr << "Analysis Error: " << parse_res.error_message << "\n";
        return 1;
    }

    if (parse_res.samples.empty()) {
        std::cerr << "Analysis Error: CSV file contains no valid sample rows.\n";
        return 1;
    }

    std::cout << "Loaded " << parse_res.samples.size() << " samples (Total lines read: "
              << parse_res.total_rows_read << ", Malformed rows skipped: "
              << parse_res.malformed_rows << ").\n\n";

    // Extract metric series
    std::vector<double> cpu_vals, ws_vals, priv_vals, thread_vals, handle_vals;
    cpu_vals.reserve(parse_res.samples.size());
    ws_vals.reserve(parse_res.samples.size());
    priv_vals.reserve(parse_res.samples.size());
    thread_vals.reserve(parse_res.samples.size());
    handle_vals.reserve(parse_res.samples.size());

    for (const auto& s : parse_res.samples) {
        cpu_vals.push_back(s.cpu_percent);
        ws_vals.push_back(static_cast<double>(s.working_set_bytes));
        priv_vals.push_back(static_cast<double>(s.private_bytes));
        thread_vals.push_back(static_cast<double>(s.thread_count));
        handle_vals.push_back(static_cast<double>(s.handle_count));
    }

    auto sum_cpu = StatsCalculator::compute_summary(cpu_vals);
    auto sum_ws = StatsCalculator::compute_summary(ws_vals);
    auto sum_priv = StatsCalculator::compute_summary(priv_vals);
    auto sum_th = StatsCalculator::compute_summary(thread_vals);
    auto sum_hd = StatsCalculator::compute_summary(handle_vals);

    std::cout << "========================================================================================================\n"
              << "                                     STATISTICAL METRICS SUMMARY                                        \n"
              << "========================================================================================================\n"
              << std::left << std::setw(18) << "Metric"
              << std::right
              << std::setw(12) << "Min"
              << std::setw(12) << "Mean"
              << std::setw(12) << "p50 (Med)"
              << std::setw(12) << "p95"
              << std::setw(12) << "p99"
              << std::setw(12) << "Max"
              << std::setw(12) << "StdDev" << "\n"
              << std::string(104, '-') << "\n";

    auto print_row = [](const std::string& name, const MetricSummary& m, bool is_bytes, bool is_pct) {
        std::cout << std::left << std::setw(18) << name << std::right;
        if (is_bytes) {
            std::cout << std::setw(12) << StringUtils::format_bytes(static_cast<uint64_t>(m.min))
                      << std::setw(12) << StringUtils::format_bytes(static_cast<uint64_t>(m.mean))
                      << std::setw(12) << StringUtils::format_bytes(static_cast<uint64_t>(m.p50))
                      << std::setw(12) << StringUtils::format_bytes(static_cast<uint64_t>(m.p95))
                      << std::setw(12) << StringUtils::format_bytes(static_cast<uint64_t>(m.p99))
                      << std::setw(12) << StringUtils::format_bytes(static_cast<uint64_t>(m.max))
                      << std::setw(12) << StringUtils::format_bytes(static_cast<uint64_t>(m.stddev)) << "\n";
        } else if (is_pct) {
            std::cout << std::fixed << std::setprecision(1)
                      << std::setw(11) << m.min << "%"
                      << std::setw(11) << m.mean << "%"
                      << std::setw(11) << m.p50 << "%"
                      << std::setw(11) << m.p95 << "%"
                      << std::setw(11) << m.p99 << "%"
                      << std::setw(11) << m.max << "%"
                      << std::setw(11) << m.stddev << "%\n";
        } else {
            std::cout << std::fixed << std::setprecision(1)
                      << std::setw(12) << m.min
                      << std::setw(12) << m.mean
                      << std::setw(12) << m.p50
                      << std::setw(12) << m.p95
                      << std::setw(12) << m.p99
                      << std::setw(12) << m.max
                      << std::setw(12) << m.stddev << "\n";
        }
    };

    if (sum_cpu) print_row("CPU %", *sum_cpu, false, true);
    if (sum_ws) print_row("Working Set", *sum_ws, true, false);
    if (sum_priv) print_row("Private Bytes", *sum_priv, true, false);
    if (sum_th) print_row("Threads", *sum_th, false, false);
    if (sum_hd) print_row("Handles", *sum_hd, false, false);

    std::cout << "========================================================================================================\n\n";

    // Anomaly Detection
    AnomalyDetector detector;
    const auto anomalies = detector.detect_anomalies(parse_res.samples);

    std::cout << "========================================================================================================\n"
              << "                                     ANOMALY DETECTION REPORT                                           \n"
              << "========================================================================================================\n";

    if (anomalies.empty()) {
        std::cout << "No anomalies detected. Process behavior appears stable and within normal baseline parameters.\n";
    } else {
        std::cout << "Identified " << anomalies.size() << " anomaly event(s):\n\n";
        for (size_t i = 0; i < anomalies.size(); ++i) {
            const auto& a = anomalies[i];
            std::cout << " [" << (i + 1) << "] " << a.title << "\n"
                      << "     Description: " << a.description << "\n"
                      << "     Rule Logic:  " << a.rule_explanation << "\n\n";
        }
    }
    std::cout << "========================================================================================================\n";

    return 0;
}

} // namespace perfscope

int main(int argc, char* argv[]) {
    perfscope::CliOptions opt = perfscope::CliParser::parse(argc, argv);

    if (opt.show_help) {
        perfscope::CliParser::print_help();
        return 0;
    }

    if (opt.show_version) {
        perfscope::CliParser::print_version();
        return 0;
    }

    if (!opt.error_message.empty()) {
        std::cerr << "Error: " << opt.error_message << "\n\n";
        perfscope::CliParser::print_help();
        return 1;
    }

    switch (opt.mode) {
        case perfscope::CommandMode::WATCH:
            return perfscope::handle_watch(opt);
        case perfscope::CommandMode::TOP:
            return perfscope::handle_top(opt);
        case perfscope::CommandMode::ANALYZE:
            return perfscope::handle_analyze(opt);
        case perfscope::CommandMode::BENCH:
            perfscope::BenchmarkRunner::run_all();
            return 0;
        default:
            perfscope::CliParser::print_help();
            return 0;
    }
}

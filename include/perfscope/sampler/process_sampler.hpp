#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace perfscope {

/**
 * @brief Represents a single point-in-time performance measurement of a target process.
 */
struct ProcessSample {
    std::string timestamp_iso;   // ISO-8601 formatted timestamp (e.g. 2026-10-18T18:00:00.123Z)
    uint64_t timestamp_ms{0};    // Milliseconds since epoch (used for interval and delta math)
    uint32_t pid{0};             // Process ID
    double cpu_percent{0.0};     // Normalized CPU percentage (0.0 to 100.0%) across all cores
    uint64_t working_set_bytes{0}; // Physical memory pages currently resident in RAM
    uint64_t private_bytes{0};   // Non-shareable committed memory (indicative of leaks)
    uint32_t thread_count{0};    // Number of active execution threads owned by the process
    uint32_t handle_count{0};    // Number of open kernel object handles
};

/**
 * @brief Information about an active process on the system (used by `top` command).
 */
struct ProcessInfo {
    uint32_t pid{0};
    std::string name;
    double cpu_percent{0.0};
    uint64_t working_set_bytes{0};
    uint64_t private_bytes{0};
    uint32_t thread_count{0};
    uint32_t handle_count{0};
};

/**
 * @brief Platform-agnostic interface for sampling process and system metrics.
 * 
 * Implementations:
 * - WindowsSampler: Uses Win32 APIs (GetProcessTimes, GetProcessMemoryInfo, ToolHelp32, GetProcessHandleCount)
 * - LinuxSampler: Reads Linux pseudo-filesystems (/proc/[pid]/stat, /proc/[pid]/status, /proc/[pid]/fd)
 */
class ProcessSampler {
public:
    virtual ~ProcessSampler() = default;

    /**
     * @brief Check whether a process with the given PID is currently running and accessible.
     */
    virtual bool is_process_running(uint32_t pid) = 0;

    /**
     * @brief Retrieve the executable name or image name for a given PID.
     */
    virtual std::string get_process_name(uint32_t pid) = 0;

    /**
     * @brief Capture a sample of metrics for a target process.
     * 
     * Note: CPU % requires at least two samples over time to compute delta CPU time / delta wall time.
     * The first sample taken by an instance for a PID will establish the baseline and report 0.0% CPU.
     * 
     * @param pid Target Process ID
     * @param out_sample Output struct filled with metrics if successful
     * @param out_error Output error message if sampling fails (e.g., Access Denied, Process Exited)
     * @return true if sampling succeeded, false otherwise
     */
    virtual bool sample_process(uint32_t pid, ProcessSample& out_sample, std::string& out_error) = 0;

    /**
     * @brief Enumerate active processes across the entire system.
     * Used for `perfscope top`.
     */
    virtual std::vector<ProcessInfo> list_processes() = 0;

    /**
     * @brief Number of logical CPU cores on the system (used for CPU % normalization).
     */
    virtual uint32_t get_cpu_core_count() const = 0;
};

} // namespace perfscope

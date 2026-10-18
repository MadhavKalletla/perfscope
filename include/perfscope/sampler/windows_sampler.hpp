#pragma once

#if defined(_WIN32)

#include "perfscope/sampler/process_sampler.hpp"
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <unordered_map>
#include <chrono>

namespace perfscope {

/**
 * @brief Previous measurement state for calculating CPU utilization delta.
 */
struct ProcessCpuBaseline {
    uint64_t last_kernel_time{0};
    uint64_t last_user_time{0};
    std::chrono::steady_clock::time_point last_wall_time;
};

class WindowsSampler : public ProcessSampler {
public:
    WindowsSampler();
    ~WindowsSampler() override = default;

    bool is_process_running(uint32_t pid) override;
    std::string get_process_name(uint32_t pid) override;
    bool sample_process(uint32_t pid, ProcessSample& out_sample, std::string& out_error) override;
    std::vector<ProcessInfo> list_processes() override;
    uint32_t get_cpu_core_count() const override { return cpu_core_count_; }

private:
    uint32_t cpu_core_count_{1};
    std::unordered_map<uint32_t, ProcessCpuBaseline> baselines_;

    // Converts FILETIME struct to 64-bit integer (100-nanosecond units)
    static uint64_t filetime_to_uint64(const FILETIME& ft);

    // Queries thread count via ToolHelp32 snapshot
    uint32_t query_thread_count(uint32_t pid);
};

} // namespace perfscope

#endif // _WIN32

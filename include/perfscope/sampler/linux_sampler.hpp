#pragma once

#if !defined(_WIN32)

#include "perfscope/sampler/process_sampler.hpp"
#include <unordered_map>
#include <chrono>

namespace perfscope {

struct LinuxCpuBaseline {
    uint64_t last_utime{0};
    uint64_t last_stime{0};
    std::chrono::steady_clock::time_point last_wall_time;
};

class LinuxSampler : public ProcessSampler {
public:
    LinuxSampler();
    ~LinuxSampler() override = default;

    bool is_process_running(uint32_t pid) override;
    std::string get_process_name(uint32_t pid) override;
    bool sample_process(uint32_t pid, ProcessSample& out_sample, std::string& out_error) override;
    std::vector<ProcessInfo> list_processes() override;
    uint32_t get_cpu_core_count() const override { return cpu_core_count_; }

private:
    uint32_t cpu_core_count_{1};
    long clk_tck_{100};
    std::unordered_map<uint32_t, LinuxCpuBaseline> baselines_;

    bool parse_proc_stat(uint32_t pid, uint64_t& utime, uint64_t& stime, uint32_t& threads);
    bool parse_proc_status(uint32_t pid, uint64_t& vm_rss, uint64_t& vm_data);
    uint32_t count_open_fds(uint32_t pid);
};

} // namespace perfscope

#endif // !_WIN32

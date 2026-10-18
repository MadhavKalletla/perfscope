#if !defined(_WIN32)

#include "perfscope/sampler/linux_sampler.hpp"
#include "perfscope/utils/time_utils.hpp"
#include <unistd.h>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <cctype>

namespace perfscope {

LinuxSampler::LinuxSampler() {
    const long cores = sysconf(_SC_NPROCESSORS_ONLN);
    cpu_core_count_ = (cores > 0) ? static_cast<uint32_t>(cores) : 1;
    clk_tck_ = sysconf(_SC_CLK_TCK);
    if (clk_tck_ <= 0) clk_tck_ = 100;
}

bool LinuxSampler::is_process_running(uint32_t pid) {
    if (pid == 0) return false;
    const std::string path = "/proc/" + std::to_string(pid);
    return access(path.c_str(), F_OK) == 0;
}

std::string LinuxSampler::get_process_name(uint32_t pid) {
    const std::string comm_path = "/proc/" + std::to_string(pid) + "/comm";
    std::ifstream comm_file(comm_path);
    if (comm_file.is_open()) {
        std::string name;
        if (std::getline(comm_file, name)) {
            if (!name.empty() && name.back() == '\n') name.pop_back();
            return name;
        }
    }
    return "Unknown (PID: " + std::to_string(pid) + ")";
}

bool LinuxSampler::parse_proc_stat(uint32_t pid, uint64_t& utime, uint64_t& stime, uint32_t& threads) {
    const std::string stat_path = "/proc/" + std::to_string(pid) + "/stat";
    std::ifstream file(stat_path);
    if (!file.is_open()) return false;

    std::string line;
    if (!std::getline(file, line)) return false;

    // Format: pid (comm) state ppid pgrp session tty_nr tpgid flags minflt cminflt majflt cmajflt utime stime cutime cstime priority nice num_threads ...
    // Since comm can contain spaces, find the last closing parenthesis ')'
    const auto close_paren = line.rfind(')');
    if (close_paren == std::string::npos || close_paren + 2 >= line.size()) return false;

    std::istringstream iss(line.substr(close_paren + 2));
    char state;
    int ppid, pgrp, session, tty_nr, tpgid;
    unsigned int flags;
    unsigned long minflt, cminflt, majflt, cmajflt;
    unsigned long u_val, s_val;
    long cutime, cstime, priority, nice, num_threads;

    if (iss >> state >> ppid >> pgrp >> session >> tty_nr >> tpgid >> flags
            >> minflt >> cminflt >> majflt >> cmajflt
            >> u_val >> s_val >> cutime >> cstime >> priority >> nice >> num_threads) {
        utime = u_val;
        stime = s_val;
        threads = (num_threads > 0) ? static_cast<uint32_t>(num_threads) : 1;
        return true;
    }
    return false;
}

bool LinuxSampler::parse_proc_status(uint32_t pid, uint64_t& vm_rss, uint64_t& vm_data) {
    const std::string status_path = "/proc/" + std::to_string(pid) + "/status";
    std::ifstream file(status_path);
    if (!file.is_open()) return false;

    std::string line;
    vm_rss = 0;
    vm_data = 0;

    while (std::getline(file, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            std::istringstream iss(line.substr(6));
            uint64_t kb = 0;
            if (iss >> kb) vm_rss = kb * 1024;
        } else if (line.rfind("VmData:", 0) == 0) {
            std::istringstream iss(line.substr(7));
            uint64_t kb = 0;
            if (iss >> kb) vm_data = kb * 1024;
        }
    }
    return true;
}

uint32_t LinuxSampler::count_open_fds(uint32_t pid) {
    const std::string fd_dir = "/proc/" + std::to_string(pid) + "/fd";
    DIR* dir = opendir(fd_dir.c_str());
    if (!dir) return 0;

    uint32_t count = 0;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] != '.') {
            ++count;
        }
    }
    closedir(dir);
    return count;
}

bool LinuxSampler::sample_process(uint32_t pid, ProcessSample& out_sample, std::string& out_error) {
    if (!is_process_running(pid)) {
        out_error = "Process PID " + std::to_string(pid) + " is not running or has terminated.";
        return false;
    }

    uint64_t utime = 0, stime = 0;
    uint32_t threads = 0;
    if (!parse_proc_stat(pid, utime, stime, threads)) {
        out_error = "Failed to read /proc/" + std::to_string(pid) + "/stat (permission denied or process exited).";
        return false;
    }

    out_sample.pid = pid;
    out_sample.timestamp_ms = TimeUtils::current_time_epoch_ms();
    out_sample.timestamp_iso = TimeUtils::format_iso8601(out_sample.timestamp_ms);
    out_sample.thread_count = threads;

    // CPU % calculation
    const auto now = std::chrono::steady_clock::now();
    auto it = baselines_.find(pid);
    if (it != baselines_.end()) {
        const auto elapsed_sec = std::chrono::duration<double>(now - it->second.last_wall_time).count();
        if (elapsed_sec > 0.0) {
            const uint64_t delta_ticks = ((utime >= it->second.last_utime) ? (utime - it->second.last_utime) : 0) +
                                         ((stime >= it->second.last_stime) ? (stime - it->second.last_stime) : 0);
            const double proc_time_sec = static_cast<double>(delta_ticks) / static_cast<double>(clk_tck_);
            double pct = (proc_time_sec / (elapsed_sec * cpu_core_count_)) * 100.0;
            pct = std::clamp(pct, 0.0, 100.0);
            out_sample.cpu_percent = pct;
        }
        it->second.last_utime = utime;
        it->second.last_stime = stime;
        it->second.last_wall_time = now;
    } else {
        baselines_[pid] = {utime, stime, now};
        out_sample.cpu_percent = 0.0;
    }

    // Memory info
    uint64_t rss = 0, data = 0;
    parse_proc_status(pid, rss, data);
    out_sample.working_set_bytes = rss;
    out_sample.private_bytes = data;

    // Open file descriptors (handles)
    out_sample.handle_count = count_open_fds(pid);

    return true;
}

std::vector<ProcessInfo> LinuxSampler::list_processes() {
    std::vector<ProcessInfo> results;
    DIR* proc_dir = opendir("/proc");
    if (!proc_dir) return results;

    struct dirent* entry;
    while ((entry = readdir(proc_dir)) != nullptr) {
        if (isdigit(entry->d_name[0])) {
            const uint32_t pid = static_cast<uint32_t>(std::stoul(entry->d_name));
            ProcessInfo info;
            info.pid = pid;
            info.name = get_process_name(pid);

            uint64_t utime = 0, stime = 0;
            uint32_t threads = 0;
            if (parse_proc_stat(pid, utime, stime, threads)) {
                info.thread_count = threads;
            }

            uint64_t rss = 0, data = 0;
            if (parse_proc_status(pid, rss, data)) {
                info.working_set_bytes = rss;
                info.private_bytes = data;
            }

            info.handle_count = count_open_fds(pid);
            results.push_back(info);
        }
    }

    closedir(proc_dir);
    return results;
}

} // namespace perfscope

#endif // !_WIN32

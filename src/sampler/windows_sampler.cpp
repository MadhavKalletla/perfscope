#if defined(_WIN32)

#include "perfscope/sampler/windows_sampler.hpp"
#include "perfscope/utils/time_utils.hpp"
#include <iostream>
#include <algorithm>
#include <system_error>

namespace perfscope {

WindowsSampler::WindowsSampler() {
    SYSTEM_INFO sys_info;
    GetSystemInfo(&sys_info);
    cpu_core_count_ = (sys_info.dwNumberOfProcessors > 0) ? sys_info.dwNumberOfProcessors : 1;
}

uint64_t WindowsSampler::filetime_to_uint64(const FILETIME& ft) {
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return uli.QuadPart;
}

bool WindowsSampler::is_process_running(uint32_t pid) {
    if (pid == 0) return false;
    HANDLE h_proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!h_proc) return false;

    DWORD exit_code = 0;
    const BOOL ok = GetExitCodeProcess(h_proc, &exit_code);
    CloseHandle(h_proc);
    return ok && (exit_code == STILL_ACTIVE);
}

std::string WindowsSampler::get_process_name(uint32_t pid) {
    if (pid == 0) return "System Idle Process";
    if (pid == 4) return "System";

    HANDLE h_proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!h_proc) {
        return "Unknown (PID: " + std::to_string(pid) + ")";
    }

    char image_path[MAX_PATH] = {0};
    DWORD path_size = MAX_PATH;
    if (QueryFullProcessImageNameA(h_proc, 0, image_path, &path_size)) {
        CloseHandle(h_proc);
        std::string full_path(image_path);
        const auto last_slash = full_path.find_last_of("\\/");
        if (last_slash != std::string::npos) {
            return full_path.substr(last_slash + 1);
        }
        return full_path;
    }

    CloseHandle(h_proc);
    return "Unknown (PID: " + std::to_string(pid) + ")";
}

uint32_t WindowsSampler::query_thread_count(uint32_t pid) {
    HANDLE h_snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (h_snap == INVALID_HANDLE_VALUE) {
        return 0;
    }

    THREADENTRY32 te;
    te.dwSize = sizeof(THREADENTRY32);
    uint32_t count = 0;

    if (Thread32First(h_snap, &te)) {
        do {
            if (te.th32OwnerProcessID == static_cast<DWORD>(pid)) {
                ++count;
            }
        } while (Thread32Next(h_snap, &te));
    }

    CloseHandle(h_snap);
    return count;
}

bool WindowsSampler::sample_process(uint32_t pid, ProcessSample& out_sample, std::string& out_error) {
    // Open handle with permissions to query timing and memory counters
    HANDLE h_proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, static_cast<DWORD>(pid));
    if (!h_proc) {
        // Attempt with limited information only (e.g. system services where VM_READ is denied)
        h_proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    }

    if (!h_proc) {
        const DWORD err = GetLastError();
        if (err == ERROR_ACCESS_DENIED) {
            out_error = "Access denied when opening PID " + std::to_string(pid) + ". Run with administrator privileges to monitor system processes.";
        } else if (err == ERROR_INVALID_PARAMETER) {
            out_error = "Process PID " + std::to_string(pid) + " not found or has already terminated.";
        } else {
            out_error = "Failed to open process PID " + std::to_string(pid) + " (Win32 Error: " + std::to_string(err) + ").";
        }
        return false;
    }

    // Verify process is still running
    DWORD exit_code = 0;
    if (GetExitCodeProcess(h_proc, &exit_code) && exit_code != STILL_ACTIVE) {
        CloseHandle(h_proc);
        out_error = "Process PID " + std::to_string(pid) + " has exited.";
        return false;
    }

    out_sample.pid = pid;
    out_sample.timestamp_ms = TimeUtils::current_time_epoch_ms();
    out_sample.timestamp_iso = TimeUtils::format_iso8601(out_sample.timestamp_ms);

    // 1. Process Times (Kernel & User)
    FILETIME creation_time{}, exit_time{}, kernel_time{}, user_time{};
    if (GetProcessTimes(h_proc, &creation_time, &exit_time, &kernel_time, &user_time)) {
        const uint64_t current_ktime = filetime_to_uint64(kernel_time);
        const uint64_t current_utime = filetime_to_uint64(user_time);
        const auto now = std::chrono::steady_clock::now();

        auto it = baselines_.find(pid);
        if (it != baselines_.end()) {
            const auto elapsed_sec = std::chrono::duration<double>(now - it->second.last_wall_time).count();
            if (elapsed_sec > 0.0) {
                const uint64_t delta_kernel = (current_ktime >= it->second.last_kernel_time) 
                    ? (current_ktime - it->second.last_kernel_time) : 0;
                const uint64_t delta_user = (current_utime >= it->second.last_user_time) 
                    ? (current_utime - it->second.last_user_time) : 0;
                
                // 1 second = 10,000,000 100-nanosecond intervals
                const double delta_proc_sec = static_cast<double>(delta_kernel + delta_user) / 10000000.0;
                // Normalize CPU across all available logical cores
                double pct = (delta_proc_sec / (elapsed_sec * cpu_core_count_)) * 100.0;
                pct = std::clamp(pct, 0.0, 100.0);
                out_sample.cpu_percent = pct;
            }
            it->second.last_kernel_time = current_ktime;
            it->second.last_user_time = current_utime;
            it->second.last_wall_time = now;
        } else {
            // First sample records baseline
            baselines_[pid] = {current_ktime, current_utime, now};
            out_sample.cpu_percent = 0.0;
        }
    }

    // 2. Memory Information (Working Set & Private Bytes)
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(PROCESS_MEMORY_COUNTERS_EX);
    if (GetProcessMemoryInfo(h_proc, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
        out_sample.working_set_bytes = pmc.WorkingSetSize;
        out_sample.private_bytes = pmc.PrivateUsage;
    }

    // 3. Handle Count
    DWORD handle_count = 0;
    if (GetProcessHandleCount(h_proc, &handle_count)) {
        out_sample.handle_count = handle_count;
    }

    CloseHandle(h_proc);

    // 4. Thread Count
    out_sample.thread_count = query_thread_count(pid);

    return true;
}

std::vector<ProcessInfo> WindowsSampler::list_processes() {
    std::vector<ProcessInfo> results;
    HANDLE h_snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (h_snap == INVALID_HANDLE_VALUE) {
        return results;
    }

    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(PROCESSENTRY32);

    if (Process32First(h_snap, &pe)) {
        do {
            ProcessInfo info;
            info.pid = pe.th32ProcessID;
            info.name = pe.szExeFile;
            info.thread_count = pe.cntThreads;

            if (info.pid > 0) {
                HANDLE h_proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
                if (!h_proc) {
                    h_proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
                }

                if (h_proc) {
                    PROCESS_MEMORY_COUNTERS_EX pmc{};
                    pmc.cb = sizeof(PROCESS_MEMORY_COUNTERS_EX);
                    if (GetProcessMemoryInfo(h_proc, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
                        info.working_set_bytes = pmc.WorkingSetSize;
                        info.private_bytes = pmc.PrivateUsage;
                    }

                    DWORD handles = 0;
                    if (GetProcessHandleCount(h_proc, &handles)) {
                        info.handle_count = handles;
                    }

                    CloseHandle(h_proc);
                }
            }

            results.push_back(info);
        } while (Process32Next(h_snap, &pe));
    }

    CloseHandle(h_snap);
    return results;
}

} // namespace perfscope

#endif // _WIN32

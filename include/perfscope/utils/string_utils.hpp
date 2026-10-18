#pragma once

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <algorithm>

namespace perfscope {

class StringUtils {
public:
    // Format byte count to human-readable string (B, KB, MB, GB)
    static std::string format_bytes(uint64_t bytes) {
        constexpr double KB = 1024.0;
        constexpr double MB = KB * 1024.0;
        constexpr double GB = MB * 1024.0;

        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);

        if (bytes >= GB) {
            oss << (static_cast<double>(bytes) / GB) << " GB";
        } else if (bytes >= MB) {
            oss << (static_cast<double>(bytes) / MB) << " MB";
        } else if (bytes >= KB) {
            oss << (static_cast<double>(bytes) / KB) << " KB";
        } else {
            oss << bytes << " B";
        }
        return oss.str();
    }

    // Trim whitespace from beginning and end of string
    static std::string trim(const std::string& str) {
        const auto start = str.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        const auto end = str.find_last_not_of(" \t\r\n");
        return str.substr(start, end - start + 1);
    }
};

} // namespace perfscope

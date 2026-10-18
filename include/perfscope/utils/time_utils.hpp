#pragma once

#include <chrono>
#include <iomanip>
#include <sstream>
#include <string>

namespace perfscope {

/**
 * @brief High-precision time and formatting utilities.
 */
class TimeUtils {
public:
    // Returns current system time in milliseconds since Unix epoch
    static uint64_t current_time_epoch_ms() {
        const auto now = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
    }

    // Formats a given epoch millisecond timestamp into ISO-8601 string: YYYY-MM-DDTHH:MM:SS.mmmZ
    static std::string format_iso8601(uint64_t epoch_ms) {
        const std::chrono::milliseconds ms_dur(epoch_ms);
        const std::chrono::time_point<std::chrono::system_clock> tp(ms_dur);
        const std::time_t tt = std::chrono::system_clock::to_time_t(tp);
        
        std::tm gm_tm{};
#if defined(_WIN32)
        gmtime_s(&gm_tm, &tt);
#else
        gmtime_r(&tt, &gm_tm);
#endif
        const auto millis = epoch_ms % 1000;
        std::ostringstream oss;
        oss << std::put_time(&gm_tm, "%Y-%m-%dT%H:%M:%S")
            << '.' << std::setfill('0') << std::setw(3) << millis << 'Z';
        return oss.str();
    }

    // Returns the current time formatted as ISO-8601
    static std::string current_iso8601() {
        return format_iso8601(current_time_epoch_ms());
    }
};

} // namespace perfscope

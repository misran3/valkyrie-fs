#include "logger.hpp"
#include <iomanip>

namespace valkyrie {

void Logger::log(LogLevel level, const std::string& component, const std::string& message) {
    // Simple structured output: TIMESTAMP [LEVEL] component: message
    std::cout << get_timestamp() << " "
              << "[" << level_to_string(level) << "] "
              << component << ": "
              << message << "\n";
}

const char* Logger::level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO: return "INFO";
        case LogLevel::WARN: return "WARN";
        case LogLevel::ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

std::string Logger::get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ) % 1000;

    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms.count();

    return oss.str();
}

}  // namespace valkyrie

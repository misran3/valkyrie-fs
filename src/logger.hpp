#pragma once

#include <string>
#include <chrono>
#include <iostream>
#include <sstream>
#include <mutex>

namespace valkyrie {

enum class LogLevel {
    DEBUG,
    INFO,
    WARN,
    ERROR
};

class Logger {
public:
    static void log(LogLevel level, const std::string& component, const std::string& message);

    static void debug(const std::string& component, const std::string& message) {
        log(LogLevel::DEBUG, component, message);
    }

    static void info(const std::string& component, const std::string& message) {
        log(LogLevel::INFO, component, message);
    }

    static void warn(const std::string& component, const std::string& message) {
        log(LogLevel::WARN, component, message);
    }

    static void error(const std::string& component, const std::string& message) {
        log(LogLevel::ERROR, component, message);
    }

private:
    static const char* level_to_string(LogLevel level);
    static std::string get_timestamp();
};

}  // namespace valkyrie

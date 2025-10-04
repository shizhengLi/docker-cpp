#pragma once

#include <string>
#include <memory>
#include <functional>
#include <vector>
#include <thread>
#include <mutex>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <unordered_map>
#include <atomic>
#include <chrono>

namespace docker_cpp {

enum class LogLevel {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARNING = 3,
    ERROR = 4,
    CRITICAL = 5
};

struct LogMessage {
    LogLevel level;
    std::string message;
    std::string logger_name;
    std::thread::id thread_id;
    std::chrono::system_clock::time_point timestamp;
};

using LogSink = std::function<void(const LogMessage&)>;

class Logger {
public:
    static Logger* getInstance(const std::string& name = "default");
    static void resetInstance(const std::string& name = "default");

    ~Logger();

    // Configuration methods
    void setLevel(LogLevel level);
    LogLevel getLevel() const;
    bool isLevelEnabled(LogLevel level) const;
    void setPattern(const std::string& pattern);
    void setConsoleSinkEnabled(bool enabled);

    // Logging methods
    template<typename... Args>
    void trace(const std::string& format, Args&&... args);

    template<typename... Args>
    void debug(const std::string& format, Args&&... args);

    template<typename... Args>
    void info(const std::string& format, Args&&... args);

    template<typename... Args>
    void warning(const std::string& format, Args&&... args);

    template<typename... Args>
    void error(const std::string& format, Args&&... args);

    template<typename... Args>
    void critical(const std::string& format, Args&&... args);

    // Sink management
    void addSink(LogSink sink, LogLevel level = LogLevel::TRACE);
    void addFileSink(const std::filesystem::path& file_path, LogLevel level = LogLevel::INFO);
    void removeFileSink(const std::filesystem::path& file_path);
    void clearSinks();

    // Utility methods
    void flush();
    std::string getName() const;

private:
    Logger(const std::string& name);

    // Non-copyable, non-movable
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;

    void log(LogLevel level, const std::string& message);
    void logToFile(const std::filesystem::path& file_path, const LogMessage& message);
    std::string formatMessage(const LogMessage& message) const;
    std::string formatString(const std::string& format) const;

    template<typename... Args>
    std::string formatString(const std::string& format, Args&&... args) const;

    static std::unordered_map<std::string, std::unique_ptr<Logger>> instances_;
    static std::mutex instances_mutex_;

    std::string name_;
    LogLevel level_;
    std::string pattern_;
    std::atomic<bool> console_sink_enabled_;

    struct SinkInfo {
        LogSink sink;
        LogLevel level;
    };

    std::vector<SinkInfo> sinks_;
    std::unordered_map<std::filesystem::path, std::unique_ptr<std::ofstream>,
                       std::hash<std::string>> file_sinks_;
    std::mutex sinks_mutex_;
    std::mutex file_sinks_mutex_;
};

// Utility functions
std::string toString(LogLevel level);
LogLevel fromString(const std::string& level_str);

// Template implementations
template<typename... Args>
void Logger::trace(const std::string& format, Args&&... args) {
    if (isLevelEnabled(LogLevel::TRACE)) {
        log(LogLevel::TRACE, formatString(format, std::forward<Args>(args)...));
    }
}

template<typename... Args>
void Logger::debug(const std::string& format, Args&&... args) {
    if (isLevelEnabled(LogLevel::DEBUG)) {
        log(LogLevel::DEBUG, formatString(format, std::forward<Args>(args)...));
    }
}

template<typename... Args>
void Logger::info(const std::string& format, Args&&... args) {
    if (isLevelEnabled(LogLevel::INFO)) {
        log(LogLevel::INFO, formatString(format, std::forward<Args>(args)...));
    }
}

template<typename... Args>
void Logger::warning(const std::string& format, Args&&... args) {
    if (isLevelEnabled(LogLevel::WARNING)) {
        log(LogLevel::WARNING, formatString(format, std::forward<Args>(args)...));
    }
}

template<typename... Args>
void Logger::error(const std::string& format, Args&&... args) {
    if (isLevelEnabled(LogLevel::ERROR)) {
        log(LogLevel::ERROR, formatString(format, std::forward<Args>(args)...));
    }
}

template<typename... Args>
void Logger::critical(const std::string& format, Args&&... args) {
    if (isLevelEnabled(LogLevel::CRITICAL)) {
        log(LogLevel::CRITICAL, formatString(format, std::forward<Args>(args)...));
    }
}

template<typename... Args>
std::string Logger::formatString(const std::string& format, Args&&... args) const {
    // Handle printf-style format strings with {} placeholders
    std::string result = format;

    // Replace {} placeholders one by one
    size_t pos = 0;
    auto format_arg = [&](auto&& arg) {
        size_t brace_pos = result.find("{}", pos);
        if (brace_pos != std::string::npos) {
            std::ostringstream oss;
            oss << arg;
            result.replace(brace_pos, 2, oss.str());
            pos = brace_pos + oss.str().length();
        }
    };

    (format_arg(args), ...);

    return result;
}

} // namespace docker_cpp
#include <algorithm>
#include <cstdio>
#include <docker-cpp/core/logger.hpp>
#include <iomanip>

namespace docker_cpp {

// Static member definitions
std::unordered_map<std::string, std::unique_ptr<Logger>> Logger::instances_;
std::mutex Logger::instances_mutex_;

Logger* Logger::getInstance(const std::string& name)
{
    std::lock_guard<std::mutex> lock(instances_mutex_);

    auto it = instances_.find(name);
    if (it == instances_.end()) {
        instances_[name] = std::unique_ptr<Logger>(new Logger(name));
        it = instances_.find(name);
    }

    return it->second.get();
}

void Logger::resetInstance(const std::string& name)
{
    std::lock_guard<std::mutex> lock(instances_mutex_);
    instances_.erase(name);
}

Logger::Logger(const std::string& name)
    : name_(name), level_(LogLevel::INFO),
      pattern_("[%l] %n: %v") // Default pattern: [level] name: message
      ,
      console_sink_enabled_(true)
{}

Logger::~Logger()
{
    flush();

    // Close all file sinks
    std::lock_guard<std::mutex> lock(file_sinks_mutex_);
    file_sinks_.clear();
}

void Logger::setLevel(LogLevel level)
{
    level_ = level;
}

LogLevel Logger::getLevel() const
{
    return level_;
}

bool Logger::isLevelEnabled(LogLevel level) const
{
    return level >= level_;
}

void Logger::setPattern(const std::string& pattern)
{
    pattern_ = pattern;
}

void Logger::setConsoleSinkEnabled(bool enabled)
{
    console_sink_enabled_ = enabled;
}

void Logger::addSink(LogSink sink, LogLevel level)
{
    std::lock_guard<std::mutex> lock(sinks_mutex_);
    sinks_.push_back({std::move(sink), level});
}

void Logger::addFileSink(const std::filesystem::path& file_path, LogLevel level)
{
    std::lock_guard<std::mutex> lock(file_sinks_mutex_);

    // Create directory if it doesn't exist
    std::filesystem::path dir = file_path.parent_path();
    if (!dir.empty() && !std::filesystem::exists(dir)) {
        std::filesystem::create_directories(dir);
    }

    // Create or get file stream
    auto& file_stream = file_sinks_[file_path];
    if (!file_stream) {
        file_stream = std::make_unique<std::ofstream>(file_path, std::ios::app);
        if (!file_stream->is_open()) {
            throw std::runtime_error("Failed to open log file: " + file_path.string());
        }
    }

    // Create file sink
    LogSink file_sink = [this, file_path](const LogMessage& message) {
        logToFile(file_path, message);
    };

    addSink(std::move(file_sink), level);
}

void Logger::removeFileSink(const std::filesystem::path& file_path)
{
    std::lock_guard<std::mutex> lock(sinks_mutex_);

    // Remove from sinks vector (simplified - we clear all sinks since we can't easily identify file
    // sinks)
    sinks_.clear();

    // Close file stream
    {
        std::lock_guard<std::mutex> file_lock(file_sinks_mutex_);
        file_sinks_.erase(file_path);
    }
}

void Logger::clearSinks()
{
    std::lock_guard<std::mutex> lock(sinks_mutex_);
    sinks_.clear();

    std::lock_guard<std::mutex> file_lock(file_sinks_mutex_);
    file_sinks_.clear();
}

void Logger::flush()
{
    std::lock_guard<std::mutex> file_lock(file_sinks_mutex_);
    for (auto& [path, stream] : file_sinks_) {
        if (stream && stream->is_open()) {
            stream->flush();
        }
    }
    std::cout.flush();
}

std::string Logger::getName() const
{
    return name_;
}

void Logger::log(LogLevel level, const std::string& message)
{
    if (!isLevelEnabled(level)) {
        return;
    }

    LogMessage log_message;
    log_message.level = level;
    log_message.message = message;
    log_message.logger_name = name_;
    log_message.thread_id = std::this_thread::get_id();
    log_message.timestamp = std::chrono::system_clock::now();

    // Console output
    if (console_sink_enabled_) {
        std::cout << formatMessage(log_message) << std::endl;
    }

    // Custom sinks
    {
        std::lock_guard<std::mutex> lock(sinks_mutex_);
        for (const auto& sink_info : sinks_) {
            if (level >= sink_info.level) {
                sink_info.sink(log_message);
            }
        }
    }
}

void Logger::logToFile(const std::filesystem::path& file_path, const LogMessage& message)
{
    std::lock_guard<std::mutex> lock(file_sinks_mutex_);

    auto it = file_sinks_.find(file_path);
    if (it != file_sinks_.end() && it->second && it->second->is_open()) {
        *it->second << formatMessage(message) << std::endl;
    }
}

std::string Logger::formatMessage(const LogMessage& message) const
{
    std::string result = pattern_;

    // Replace format tokens
    size_t pos = 0;
    while ((pos = result.find("%l", pos)) != std::string::npos) {
        result.replace(pos, 2, toString(message.level));
        pos += toString(message.level).length();
    }

    pos = 0;
    while ((pos = result.find("%n", pos)) != std::string::npos) {
        result.replace(pos, 2, message.logger_name);
        pos += message.logger_name.length();
    }

    pos = 0;
    while ((pos = result.find("%v", pos)) != std::string::npos) {
        result.replace(pos, 2, message.message);
        pos += message.message.length();
    }

    // Add timestamp
    pos = 0;
    while ((pos = result.find("%t", pos)) != std::string::npos) {
        auto time_t = std::chrono::system_clock::to_time_t(message.timestamp);
        std::ostringstream time_stream;
        time_stream << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        std::string time_str = time_stream.str();
        result.replace(pos, 2, time_str);
        pos += time_str.length();
    }

    // Add thread ID
    pos = 0;
    while ((pos = result.find("%T", pos)) != std::string::npos) {
        std::ostringstream thread_stream;
        thread_stream << message.thread_id;
        std::string thread_str = thread_stream.str();
        result.replace(pos, 2, thread_str);
        pos += thread_str.length();
    }

    return result;
}

std::string Logger::formatString(const std::string& format) const
{
    return format;
}

// Utility functions
std::string toString(LogLevel level)
{
    switch (level) {
        case LogLevel::TRACE:
            return "TRACE";
        case LogLevel::DEBUG:
            return "DEBUG";
        case LogLevel::INFO:
            return "INFO";
        case LogLevel::WARNING:
            return "WARNING";
        case LogLevel::ERROR:
            return "ERROR";
        case LogLevel::CRITICAL:
            return "CRITICAL";
        default:
            return "UNKNOWN";
    }
}

LogLevel fromString(const std::string& level_str)
{
    std::string upper_str = level_str;
    std::transform(upper_str.begin(), upper_str.end(), upper_str.begin(), ::toupper);

    if (upper_str == "TRACE")
        return LogLevel::TRACE;
    if (upper_str == "DEBUG")
        return LogLevel::DEBUG;
    if (upper_str == "INFO")
        return LogLevel::INFO;
    if (upper_str == "WARNING")
        return LogLevel::WARNING;
    if (upper_str == "ERROR")
        return LogLevel::ERROR;
    if (upper_str == "CRITICAL")
        return LogLevel::CRITICAL;

    // Default to INFO for invalid strings
    return LogLevel::INFO;
}

} // namespace docker_cpp
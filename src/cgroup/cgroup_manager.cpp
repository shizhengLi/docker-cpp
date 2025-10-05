#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <thread>

#include <docker-cpp/cgroup/cgroup_manager.hpp>
#include <docker-cpp/core/error.hpp>

namespace docker_cpp {

// Constants for cgroup v2 filesystem operations
constexpr uint64_t DEFAULT_CPU_PERIOD_US = 100000; // 100ms
constexpr uint64_t DEFAULT_CPU_QUOTA_US = 1000000; // 1s (unlimited)
constexpr uint64_t DEFAULT_CPU_WEIGHT = 100;       // Default weight
constexpr uint64_t MIN_CPU_WEIGHT = 1;             // Minimum weight
constexpr uint64_t MAX_CPU_WEIGHT = 10000;         // Maximum weight
constexpr uint64_t DEFAULT_MEMORY_LIMIT = 0;       // Unlimited
constexpr uint64_t DEFAULT_PID_MAX = 0;            // Unlimited
constexpr uint64_t MAX_PID_MAX = 4194303;          // Maximum PIDs per cgroup

// Default CgroupConfig constructor
CgroupConfig::CgroupConfig() : controllers(CgroupController::ALL), parent_path("")
{
    // Initialize CPU configuration with defaults
    cpu.max_usec = DEFAULT_CPU_QUOTA_US;
    cpu.period_usec = DEFAULT_CPU_PERIOD_US;
    cpu.weight = DEFAULT_CPU_WEIGHT;
    cpu.burst_usec = 0; // No burst by default

    // Initialize memory configuration with defaults
    memory.max_bytes = DEFAULT_MEMORY_LIMIT;
    memory.swap_max_bytes = DEFAULT_MEMORY_LIMIT;
    memory.low_bytes = 0;
    memory.high_bytes = 0;
    memory.oom_kill_enable = true;

    // Initialize IO configuration with defaults
    io.read_bps = 0;   // Unlimited
    io.write_bps = 0;  // Unlimited
    io.read_iops = 0;  // Unlimited
    io.write_iops = 0; // Unlimited

    // Initialize PID configuration with defaults
    pid.max = DEFAULT_PID_MAX;
}

// CgroupError implementation
CgroupError::CgroupError(Code code, const std::string& message) : code_(code), message_(message) {}

const char* CgroupError::what() const noexcept
{
    return message_.c_str();
}

CgroupError::Code CgroupError::getCode() const
{
    return code_;
}

// Utility functions for CgroupController operations
CgroupController operator|(CgroupController a, CgroupController b)
{
    return static_cast<CgroupController>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

CgroupController operator&(CgroupController a, CgroupController b)
{
    return static_cast<CgroupController>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

bool hasController(CgroupController controllers, CgroupController controller)
{
    return (controllers & controller) == controller;
}

// String conversion utilities
std::string cgroupControllerToString(CgroupController controller)
{
    switch (controller) {
        case CgroupController::CPU:
            return "cpu";
        case CgroupController::MEMORY:
            return "memory";
        case CgroupController::IO:
            return "io";
        case CgroupController::PID:
            return "pids";
        case CgroupController::CPUSET:
            return "cpuset";
        case CgroupController::HUGETLB:
            return "hugetlb";
        case CgroupController::RDMA:
            return "rdma";
        case CgroupController::MISC:
            return "misc";
        case CgroupController::ALL:
            return "all";
        default:
            return "unknown";
    }
}

CgroupController stringToCgroupController(const std::string& str)
{
    if (str == "cpu") {
        return CgroupController::CPU;
    }
    else if (str == "memory") {
        return CgroupController::MEMORY;
    }
    else if (str == "io") {
        return CgroupController::IO;
    }
    else if (str == "pids") {
        return CgroupController::PID;
    }
    else if (str == "cpuset") {
        return CgroupController::CPUSET;
    }
    else if (str == "hugetlb") {
        return CgroupController::HUGETLB;
    }
    else if (str == "rdma") {
        return CgroupController::RDMA;
    }
    else if (str == "misc") {
        return CgroupController::MISC;
    }
    else if (str == "all") {
        return CgroupController::ALL;
    }
    else {
        throw CgroupError(CgroupError::Code::INVALID_ARGUMENT, "Invalid controller string: " + str);
    }
}

// CgroupManager implementation
class CgroupManagerImpl : public CgroupManager {
public:
    explicit CgroupManagerImpl(const CgroupConfig& config)
        : config_(config), mount_point_(getMountPoint()), cgroup_path_(buildCgroupPath()),
          controllers_enabled_(config.controllers)
    {}

    ~CgroupManagerImpl() override
    {
        try {
            if (exists()) {
                destroy();
            }
        }
        catch (...) {
            // Ignore errors in destructor
        }
    }

    // Lifecycle management
    void create() override
    {
        if (exists()) {
            throw CgroupError(CgroupError::Code::INVALID_ARGUMENT,
                              "Cgroup already exists: " + cgroup_path_);
        }

        // Create cgroup directory
        std::error_code ec;
        std::filesystem::create_directories(cgroup_path_, ec);
        if (ec) {
            throw CgroupError(
                CgroupError::Code::IO_ERROR,
                "Failed to create cgroup directory: " + cgroup_path_ + " - " + ec.message());
        }

        // Enable controllers by writing to cgroup.subtree_control
        enableControllers();

        // Apply initial configuration
        applyConfiguration();
    }

    void destroy() override
    {
        if (!exists()) {
            return; // Already destroyed
        }

        // Remove all processes first
        try {
            std::vector<pid_t> processes = getProcesses();
            for (pid_t pid : processes) {
                removeProcess(pid);
            }
        }
        catch (...) {
            // Ignore errors during process cleanup
        }

        // Remove cgroup directory
        std::error_code ec;
        std::filesystem::remove_all(cgroup_path_, ec);
        if (ec) {
            throw CgroupError(
                CgroupError::Code::IO_ERROR,
                "Failed to remove cgroup directory: " + cgroup_path_ + " - " + ec.message());
        }
    }

    bool exists() const override
    {
        std::error_code ec;
        return std::filesystem::exists(cgroup_path_, ec)
               && std::filesystem::is_directory(cgroup_path_, ec);
    }

    // Process management
    void addProcess(pid_t pid) override
    {
        if (!exists()) {
            throw CgroupError(CgroupError::Code::NOT_FOUND,
                              "Cgroup does not exist: " + cgroup_path_);
        }

        // Verify process exists
        if (kill(pid, 0) == -1 && errno == ESRCH) {
            throw CgroupError(CgroupError::Code::PROCESS_NOT_FOUND,
                              "Process not found: " + std::to_string(pid));
        }

        // Write PID to cgroup.procs
        std::string procs_file = cgroup_path_ + "/cgroup.procs";
        std::ofstream file(procs_file);
        if (!file) {
            throw CgroupError(CgroupError::Code::IO_ERROR,
                              "Failed to open cgroup.procs: " + procs_file);
        }

        file << pid;
        if (!file) {
            throw CgroupError(CgroupError::Code::IO_ERROR,
                              "Failed to write PID to cgroup.procs: " + std::to_string(pid));
        }
    }

    void removeProcess(pid_t pid) override
    {
        if (!exists()) {
            throw CgroupError(CgroupError::Code::NOT_FOUND,
                              "Cgroup does not exist: " + cgroup_path_);
        }

        // To remove a process, we need to move it to the parent cgroup
        std::string parent_procs_file = mount_point_ + "/cgroup.procs";
        if (!config_.parent_path.empty()) {
            parent_procs_file = config_.parent_path + "/cgroup.procs";
        }

        std::ofstream file(parent_procs_file);
        if (!file) {
            throw CgroupError(CgroupError::Code::IO_ERROR,
                              "Failed to open parent cgroup.procs: " + parent_procs_file);
        }

        file << pid;
        if (!file) {
            throw CgroupError(CgroupError::Code::IO_ERROR,
                              "Failed to move PID to parent cgroup: " + std::to_string(pid));
        }
    }

    std::vector<pid_t> getProcesses() const override
    {
        if (!exists()) {
            throw CgroupError(CgroupError::Code::NOT_FOUND,
                              "Cgroup does not exist: " + cgroup_path_);
        }

        std::string procs_file = cgroup_path_ + "/cgroup.procs";
        std::ifstream file(procs_file);
        if (!file) {
            throw CgroupError(CgroupError::Code::IO_ERROR,
                              "Failed to open cgroup.procs: " + procs_file);
        }

        std::vector<pid_t> processes;
        std::string line;
        while (std::getline(file, line)) {
            try {
                pid_t pid = std::stoul(line);
                processes.push_back(pid);
            }
            catch (...) {
                // Ignore invalid lines
            }
        }

        return processes;
    }

    // Controller operations
    void enableController(CgroupController controller) override
    {
        controllers_enabled_ = controllers_enabled_ | controller;
        updateControllers();
    }

    void disableController(CgroupController controller) override
    {
        controllers_enabled_ = static_cast<CgroupController>(
            static_cast<uint32_t>(controllers_enabled_) & ~static_cast<uint32_t>(controller));
        // Note: Disabling controllers in cgroup v2 is more complex
        // For now, we just update our internal state
    }

    bool isControllerEnabled(CgroupController controller) const override
    {
        return hasController(controllers_enabled_, controller);
    }

    // Resource limits and settings
    void setCpuMax(uint64_t max_usec, uint64_t period_usec) override
    {
        validateCpuParameters(max_usec, period_usec);

        config_.cpu.max_usec = max_usec;
        config_.cpu.period_usec = period_usec;

        writeCpuMax();
    }

    void setCpuWeight(uint64_t weight) override
    {
        validateCpuWeight(weight);

        config_.cpu.weight = weight;

        std::string cpu_weight_file = cgroup_path_ + "/cpu.weight";
        writeFileValue(cpu_weight_file, std::to_string(weight));
    }

    void setCpuBurst(uint64_t burst_usec) override
    {
        config_.cpu.burst_usec = burst_usec;

        std::string cpu_max_file = cgroup_path_ + "/cpu.max";
        std::string current_value;
        try {
            current_value = readFileValue(cpu_max_file);
            // Parse current max and period, then add burst
            // Format: "max period" or "period" (if max is "max")
        }
        catch (...) {
            // If file doesn't exist, this might not be supported
        }
    }

    void setMemoryMax(uint64_t max_bytes) override
    {
        config_.memory.max_bytes = max_bytes;

        std::string memory_max_file = cgroup_path_ + "/memory.max";
        std::string value = (max_bytes == 0) ? "max" : std::to_string(max_bytes);
        writeFileValue(memory_max_file, value);
    }

    void setMemorySwapMax(uint64_t max_bytes) override
    {
        config_.memory.swap_max_bytes = max_bytes;

        std::string memory_swap_max_file = cgroup_path_ + "/memory.swap.max";
        std::string value = (max_bytes == 0) ? "max" : std::to_string(max_bytes);
        writeFileValue(memory_swap_max_file, value);
    }

    void setMemoryLow(uint64_t low_bytes) override
    {
        config_.memory.low_bytes = low_bytes;

        std::string memory_low_file = cgroup_path_ + "/memory.low";
        std::string value = (low_bytes == 0) ? "max" : std::to_string(low_bytes);
        writeFileValue(memory_low_file, value);
    }

    void setMemoryHigh(uint64_t high_bytes) override
    {
        config_.memory.high_bytes = high_bytes;

        std::string memory_high_file = cgroup_path_ + "/memory.high";
        std::string value = (high_bytes == 0) ? "max" : std::to_string(high_bytes);
        writeFileValue(memory_high_file, value);
    }

    void setOomKillEnable(bool enable) override
    {
        config_.memory.oom_kill_enable = enable;

        std::string memory_oom_group_file = cgroup_path_ + "/memory.oom.group";
        writeFileValue(memory_oom_group_file, enable ? "1" : "0");
    }

    void setIoMax(const std::string& device, uint64_t read_bps, uint64_t write_bps) override
    {
        config_.io.read_bps = read_bps;
        config_.io.write_bps = write_bps;

        std::string io_max_file = cgroup_path_ + "/io.max";
        std::string value =
            device + " rbps=" + std::to_string(read_bps) + " wbps=" + std::to_string(write_bps);
        writeFileValue(io_max_file, value);
    }

    void setIoBps(const std::string& device, uint64_t read_bps, uint64_t write_bps) override
    {
        setIoMax(device, read_bps, write_bps);
    }

    void setIoIops(const std::string& device, uint64_t read_iops, uint64_t write_iops) override
    {
        config_.io.read_iops = read_iops;
        config_.io.write_iops = write_iops;

        std::string io_max_file = cgroup_path_ + "/io.max";
        std::string value =
            device + " riops=" + std::to_string(read_iops) + " wiops=" + std::to_string(write_iops);
        writeFileValue(io_max_file, value);
    }

    void setPidMax(uint64_t max) override
    {
        validatePidMax(max);

        config_.pid.max = max;

        std::string pids_max_file = cgroup_path_ + "/pids.max";
        std::string value = (max == 0) ? "max" : std::to_string(max);
        writeFileValue(pids_max_file, value);
    }

    // Statistics and monitoring
    ResourceMetrics getMetrics() const override
    {
        ResourceMetrics metrics;
        metrics.timestamp = getCurrentTimestamp();
        metrics.cpu = getCpuStats();
        metrics.memory = getMemoryStats();
        metrics.io = getIoStats();
        metrics.pid = getPidStats();
        return metrics;
    }

    CpuStats getCpuStats() const override
    {
        CpuStats stats{};

        if (!exists()) {
            return stats;
        }

        try {
            std::string cpu_stat_file = cgroup_path_ + "/cpu.stat";
            std::string content = readFileValue(cpu_stat_file);

            std::istringstream iss(content);
            std::string line;
            while (std::getline(iss, line)) {
                std::istringstream line_stream(line);
                std::string key;
                uint64_t value;

                if (line_stream >> key >> value) {
                    if (key == "usage_usec") {
                        stats.usage_usec = value;
                    }
                    else if (key == "user_usec") {
                        stats.user_usec = value;
                    }
                    else if (key == "system_usec") {
                        stats.system_usec = value;
                    }
                    else if (key == "nr_periods") {
                        stats.nr_periods = value;
                    }
                    else if (key == "nr_throttled") {
                        stats.nr_throttled = value;
                    }
                    else if (key == "throttled_usec") {
                        stats.throttled_usec = value;
                    }
                }
            }

            // Calculate usage percentage
            stats.usage_percent = calculateCpuUsagePercent(stats);
        }
        catch (...) {
            // Return default stats if file doesn't exist or can't be read
        }

        return stats;
    }

    MemoryStats getMemoryStats() const override
    {
        MemoryStats stats{};

        if (!exists()) {
            return stats;
        }

        try {
            // Current memory usage
            std::string memory_current_file = cgroup_path_ + "/memory.current";
            stats.current = std::stoull(readFileValue(memory_current_file));

            // Memory limit
            std::string memory_max_file = cgroup_path_ + "/memory.max";
            std::string max_value = readFileValue(memory_max_file);
            stats.limit = (max_value == "max") ? 0 : std::stoull(max_value);

            // Peak memory usage
            std::string memory_peak_file = cgroup_path_ + "/memory.peak";
            stats.peak = std::stoull(readFileValue(memory_peak_file));

            // Swap usage
            std::string memory_swap_current_file = cgroup_path_ + "/memory.swap.current";
            stats.swap_current = std::stoull(readFileValue(memory_swap_current_file));

            std::string memory_swap_max_file = cgroup_path_ + "/memory.swap.max";
            std::string swap_max_value = readFileValue(memory_swap_max_file);
            stats.swap_limit = (swap_max_value == "max") ? 0 : std::stoull(swap_max_value);

            // Calculate usage percentage
            if (stats.limit > 0) {
                stats.usage_percent =
                    (static_cast<double>(stats.current) / static_cast<double>(stats.limit)) * 100.0;
            }
        }
        catch (...) {
            // Return default stats if files don't exist or can't be read
        }

        return stats;
    }

    IoStats getIoStats() const override
    {
        IoStats stats{};

        if (!exists()) {
            return stats;
        }

        try {
            std::string io_stat_file = cgroup_path_ + "/io.stat";
            std::string content = readFileValue(io_stat_file);

            // Parse io.stat format
            // Example: "8:0 rbytes=12345 wbytes=67890 rios=12 wios=34 dbytes=5678 dios=56"
            std::istringstream iss(content);
            std::string line;
            while (std::getline(iss, line)) {
                std::istringstream line_stream(line);
                std::string device_part;
                if (std::getline(line_stream, device_part, ' ')) {
                    std::istringstream stats_stream(device_part.substr(3)); // Skip "8:0 "
                    std::string stat;
                    while (std::getline(stats_stream, stat, ' ')) {
                        size_t pos = stat.find('=');
                        if (pos != std::string::npos) {
                            std::string key = stat.substr(0, pos);
                            uint64_t value = std::stoull(stat.substr(pos + 1));

                            if (key == "rbytes") {
                                stats.rbytes += value;
                            }
                            else if (key == "wbytes") {
                                stats.wbytes += value;
                            }
                            else if (key == "rios") {
                                stats.rios += value;
                            }
                            else if (key == "wios") {
                                stats.wios += value;
                            }
                            else if (key == "dbytes") {
                                stats.dbytes += value;
                            }
                            else if (key == "dios") {
                                stats.dios += value;
                            }
                        }
                    }
                }
            }
        }
        catch (...) {
            // Return default stats if file doesn't exist or can't be read
        }

        return stats;
    }

    PidStats getPidStats() const override
    {
        PidStats stats{};

        if (!exists()) {
            return stats;
        }

        try {
            // Current PID count
            std::vector<pid_t> processes = getProcesses();
            stats.current = processes.size();

            // PID max limit
            std::string pids_max_file = cgroup_path_ + "/pids.max";
            std::string max_value = readFileValue(pids_max_file);
            stats.max = (max_value == "max") ? 0 : std::stoull(max_value);
        }
        catch (...) {
            // Return default stats if file doesn't exist or can't be read
        }

        return stats;
    }

    // Configuration and state
    std::string getPath() const override
    {
        return cgroup_path_;
    }

    CgroupConfig getConfig() const override
    {
        return config_;
    }

    void updateConfig(const CgroupConfig& config) override
    {
        config_ = config;
        if (exists()) {
            applyConfiguration();
        }
    }

    // Notifications and events
    void enableMemoryPressureEvents() override
    {
        // Enable memory.pressure file for pressure level notifications
        // This is more complex and would require inotify or eventfd
    }

    void enableOomEvents() override
    {
        // Enable OOM event notifications
        // This would require eventfd and proper kernel support
    }

    bool hasMemoryPressureEvent() const override
    {
        // Check for memory pressure events
        return false; // Not implemented yet
    }

    bool hasOomEvent() const override
    {
        // Check for OOM events
        return false; // Not implemented yet
    }

    // Static utility methods
    static bool isCgroupV2Supported()
    {
        // Check if /sys/fs/cgroup is mounted as cgroup2
        std::string mount_info = readFileValue("/proc/self/mountinfo");
        return mount_info.find("cgroup2") != std::string::npos;
    }

    static std::string getMountPoint()
    {
        // Default mount point for cgroup v2
        return "/sys/fs/cgroup";
    }

    static std::vector<std::string> listControllers()
    {
        std::vector<std::string> controllers;

        try {
            std::string controllers_file = getMountPoint() + "/cgroup.controllers";
            std::string content = readFileValue(controllers_file);

            std::istringstream iss(content);
            std::string controller;
            while (iss >> controller) {
                controllers.push_back(controller);
            }
        }
        catch (...) {
            // Return empty list if can't read controllers
        }

        return controllers;
    }

    static bool isControllerAvailable(const std::string& controller)
    {
        std::vector<std::string> available_controllers = listControllers();
        return std::find(available_controllers.begin(), available_controllers.end(), controller)
               != available_controllers.end();
    }

private:
    CgroupConfig config_;
    std::string mount_point_;
    std::string cgroup_path_;
    CgroupController controllers_enabled_;

    std::string buildCgroupPath() const
    {
        if (config_.parent_path.empty()) {
            return mount_point_ + "/" + config_.name;
        }
        else {
            return config_.parent_path + "/" + config_.name;
        }
    }

    void enableControllers()
    {
        // Enable controllers in parent cgroup
        std::string parent_path = config_.parent_path.empty() ? mount_point_ : config_.parent_path;
        std::string subtree_control_file = parent_path + "/cgroup.subtree_control";

        std::string controllers_to_enable;
        if (hasController(controllers_enabled_, CgroupController::CPU)) {
            controllers_to_enable += " +cpu";
        }
        if (hasController(controllers_enabled_, CgroupController::MEMORY)) {
            controllers_to_enable += " +memory";
        }
        if (hasController(controllers_enabled_, CgroupController::IO)) {
            controllers_to_enable += " +io";
        }
        if (hasController(controllers_enabled_, CgroupController::PID)) {
            controllers_to_enable += " +pids";
        }

        if (!controllers_to_enable.empty()) {
            // Read current content first
            std::string current_content;
            try {
                current_content = readFileValue(subtree_control_file);
            }
            catch (...) {
                // File might not exist, that's okay
            }

            // Append new controllers
            std::string new_content = current_content + controllers_to_enable;
            writeFileValue(subtree_control_file, new_content);
        }
    }

    void updateControllers()
    {
        if (exists()) {
            enableControllers();
        }
    }

    void applyConfiguration()
    {
        // Apply all configuration settings
        if (hasController(controllers_enabled_, CgroupController::CPU)) {
            setCpuMax(config_.cpu.max_usec, config_.cpu.period_usec);
            setCpuWeight(config_.cpu.weight);
            if (config_.cpu.burst_usec > 0) {
                setCpuBurst(config_.cpu.burst_usec);
            }
        }

        if (hasController(controllers_enabled_, CgroupController::MEMORY)) {
            setMemoryMax(config_.memory.max_bytes);
            setMemorySwapMax(config_.memory.swap_max_bytes);
            setMemoryLow(config_.memory.low_bytes);
            setMemoryHigh(config_.memory.high_bytes);
            setOomKillEnable(config_.memory.oom_kill_enable);
        }

        if (hasController(controllers_enabled_, CgroupController::IO)) {
            // Apply IO settings if device is specified
            // This would need device detection or configuration
        }

        if (hasController(controllers_enabled_, CgroupController::PID)) {
            setPidMax(config_.pid.max);
        }
    }

    void writeCpuMax()
    {
        std::string cpu_max_file = cgroup_path_ + "/cpu.max";
        std::string value =
            (config_.cpu.max_usec == 0) ? "max" : std::to_string(config_.cpu.max_usec);
        value += " " + std::to_string(config_.cpu.period_usec);
        writeFileValue(cpu_max_file, value);
    }

    static void writeFileValue(const std::string& file_path, const std::string& value)
    {
        std::ofstream file(file_path);
        if (!file) {
            throw CgroupError(CgroupError::Code::IO_ERROR,
                              "Failed to open file for writing: " + file_path);
        }

        file << value;
        if (!file) {
            throw CgroupError(CgroupError::Code::IO_ERROR, "Failed to write to file: " + file_path);
        }
    }

    static std::string readFileValue(const std::string& file_path)
    {
        std::ifstream file(file_path);
        if (!file) {
            throw CgroupError(CgroupError::Code::IO_ERROR,
                              "Failed to open file for reading: " + file_path);
        }

        std::string content;
        std::getline(file, content);
        if (!file && !file.eof()) {
            throw CgroupError(CgroupError::Code::IO_ERROR,
                              "Failed to read from file: " + file_path);
        }

        return content;
    }

    void validateCpuParameters(uint64_t max_usec, uint64_t period_usec) const
    {
        if (period_usec < 1000 || period_usec > 1000000) {
            throw CgroupError(CgroupError::Code::INVALID_ARGUMENT,
                              "CPU period must be between 1000 and 1000000 microseconds");
        }

        if (max_usec != 0 && max_usec < 1000) {
            throw CgroupError(CgroupError::Code::INVALID_ARGUMENT,
                              "CPU max must be at least 1000 microseconds or 0 for unlimited");
        }
    }

    void validateCpuWeight(uint64_t weight) const
    {
        if (weight < MIN_CPU_WEIGHT || weight > MAX_CPU_WEIGHT) {
            throw CgroupError(CgroupError::Code::INVALID_ARGUMENT,
                              "CPU weight must be between 1 and 10000");
        }
    }

    void validatePidMax(uint64_t max) const
    {
        if (max > MAX_PID_MAX) {
            throw CgroupError(CgroupError::Code::INVALID_ARGUMENT,
                              "PID max cannot exceed " + std::to_string(MAX_PID_MAX));
        }
    }

    double calculateCpuUsagePercent(const CpuStats& stats) const
    {
        if (stats.usage_usec == 0) {
            return 0.0;
        }

        // Calculate percentage based on total time elapsed
        // This is a simplified calculation
        return std::min(100.0, (static_cast<double>(stats.usage_usec) / 1000000.0) * 100.0);
    }

    static uint64_t getCurrentTimestamp()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    }
};

// Factory method implementation
std::unique_ptr<CgroupManager> CgroupManager::create(const CgroupConfig& config)
{
    return std::make_unique<CgroupManagerImpl>(config);
}

// Static utility method implementations
bool CgroupManager::isCgroupV2Supported()
{
    std::error_code ec;
    return std::filesystem::exists("/sys/fs/cgroup/cgroup.controllers", ec);
}

std::string CgroupManager::getMountPoint()
{
    return "/sys/fs/cgroup";
}

std::vector<std::string> CgroupManager::listControllers()
{
    std::vector<std::string> controllers;

    if (!isCgroupV2Supported()) {
        return controllers;
    }

    std::ifstream file("/sys/fs/cgroup/cgroup.controllers");
    if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
            std::istringstream iss(line);
            std::string controller;
            while (iss >> controller) {
                controllers.push_back(controller);
            }
        }
    }

    return controllers;
}

bool CgroupManager::isControllerAvailable(const std::string& controller)
{
    auto controllers = listControllers();
    return std::find(controllers.begin(), controllers.end(), controller) != controllers.end();
}

} // namespace docker_cpp
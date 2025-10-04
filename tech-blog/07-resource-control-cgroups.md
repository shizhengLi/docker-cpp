# Resource Control: Cgroups Management System

## Introduction

Control Groups (cgroups) are a fundamental Linux kernel feature that enables fine-grained resource control and accounting for process groups. This article explores the implementation of a comprehensive cgroups management system in C++, covering cgroup v2, resource limits, performance monitoring, and integration with container runtimes.

## Understanding Cgroups

### Cgroups Evolution

**cgroup v1** (Linux 2.6.24+): Multiple hierarchies, per-controller mount points
**cgroup v2** (Linux 4.5+): Unified hierarchy, improved performance and usability

### Available Controllers

| Controller | Resource | Purpose | Configuration Files |
|------------|----------|---------|-------------------|
| cpu | CPU time | CPU scheduling and limits | cpu.max, cpu.weight |
| memory | Memory | Memory usage and limits | memory.max, memory.swap.max |
| io | Block I/O | Disk I/O throttling | io.max, io.weight |
| pids | Process count | Process number limits | pids.max |
| rdma | RDMA devices | Remote Direct Memory Access | rdma.max |
| misc | Miscellaneous | Kernel resource limits | misc.max |
| hugetlb | Huge pages | Huge page memory usage | hugetlb.*.max |

## Cgroup Manager Architecture

### 1. Core Cgroup Manager

```cpp
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <filesystem>
#include <chrono>
#include <thread>

class CgroupManager {
public:
    enum class Controller : uint32_t {
        CPU = 0x01,
        MEMORY = 0x02,
        IO = 0x04,
        PIDS = 0x08,
        RDMA = 0x10,
        MISC = 0x20,
        HUGETLB = 0x40,
        FREEZER = 0x80,
        DEVICES = 0x100,
        ALL = 0x1FF
    };

    explicit CgroupManager(const std::string& cgroup_path,
                          Controller controllers = Controller::CPU | Controller::MEMORY)
        : cgroup_path_(cgroup_path), controllers_(controllers), active_(false) {
        validateCgroupVersion();
    }

    ~CgroupManager() {
        if (active_) {
            try {
                destroy();
            } catch (...) {
                // Log error but don't throw in destructor
            }
        }
    }

    void create() {
        if (active_) {
            throw std::runtime_error("Cgroup already active");
        }

        createCgroupDirectory();
        enableControllers();
        setDefaultConfigurations();
        active_ = true;
    }

    void destroy() {
        if (!active_) {
            return;
        }

        // Remove all processes from cgroup
        try {
            clearProcesses();
        } catch (...) {
            // Ignore cleanup errors
        }

        // Remove cgroup directory
        if (std::filesystem::exists(cgroup_path_)) {
            std::filesystem::remove_all(cgroup_path_);
        }

        active_ = false;
    }

    void addProcess(pid_t pid) {
        validateActive();

        std::string cgroup_procs_file = cgroup_path_ + "/cgroup.procs";
        std::ofstream procs_file(cgroup_procs_file, std::ios::app);

        if (!procs_file) {
            throw std::runtime_error("Failed to open cgroup.procs file");
        }

        procs_file << pid << std::endl;

        if (!procs_file.good()) {
            throw std::runtime_error("Failed to add process to cgroup");
        }
    }

    void removeProcess(pid_t pid) {
        validateActive();

        // Move process to parent cgroup (root by default)
        std::string parent_procs = getCgroupParentPath() + "/cgroup.procs";
        std::ofstream parent_file(parent_procs, std::ios::app);

        if (parent_file) {
            parent_file << pid << std::endl;
        }
    }

    std::vector<pid_t> getProcesses() const {
        validateActive();

        std::vector<pid_t> processes;
        std::string cgroup_procs_file = cgroup_path_ + "/cgroup.procs";
        std::ifstream procs_file(cgroup_procs_file);

        if (!procs_file) {
            return processes;
        }

        pid_t pid;
        while (procs_file >> pid) {
            processes.push_back(pid);
        }

        return processes;
    }

    bool hasProcess(pid_t pid) const {
        auto processes = getProcesses();
        return std::find(processes.begin(), processes.end(), pid) != processes.end();
    }

    void freeze() {
        validateActive();
        validateController(Controller::FREEZER);

        writeValueToFile("cgroup.freeze", "1");
    }

    void thaw() {
        validateActive();
        validateController(Controller::FREEZER);

        writeValueToFile("cgroup.freeze", "0");
    }

    bool isFrozen() const {
        validateActive();
        validateController(Controller::FREEZER);

        return readValueFromFile("cgroup.freeze") == "1";
    }

private:
    std::string cgroup_path_;
    Controller controllers_;
    bool active_;
    bool cgroup_v2_;

    void validateCgroupVersion() {
        std::ifstream mountinfo("/proc/self/mountinfo");
        std::string line;

        cgroup_v2_ = false;
        while (std::getline(mountinfo, line)) {
            if (line.find("cgroup2") != std::string::npos) {
                cgroup_v2_ = true;
                break;
            }
        }

        if (!cgroup_v2_) {
            throw std::runtime_error("cgroup v2 is required");
        }
    }

    void createCgroupDirectory() {
        if (!std::filesystem::exists(cgroup_path_)) {
            if (!std::filesystem::create_directories(cgroup_path_)) {
                throw std::runtime_error("Failed to create cgroup directory");
            }
        }
    }

    void enableControllers() {
        if (!cgroup_v2_) {
            return; // Controllers are automatically enabled in v1
        }

        std::string subtree_control_file = getCgroupParentPath() + "/cgroup.subtree_control";
        std::ofstream control_file(subtree_control_file, std::ios::app);

        if (!control_file) {
            return; // Parent might not allow controller enabling
        }

        // Enable requested controllers
        if (controllers_ & Controller::CPU) {
            control_file << "+cpu ";
        }
        if (controllers_ & Controller::MEMORY) {
            control_file << "+memory ";
        }
        if (controllers_ & Controller::IO) {
            control_file << "+io ";
        }
        if (controllers_ & Controller::PIDS) {
            control_file << "+pids ";
        }
        if (controllers_ & Controller::RDMA) {
            control_file << "+rdma ";
        }
        if (controllers_ & Controller::MISC) {
            control_file << "+misc ";
        }
        if (controllers_ & Controller::HUGETLB) {
            control_file << "+hugetlb ";
        }

        control_file << std::endl;
    }

    void setDefaultConfigurations() {
        // Set safe default values
        if (controllers_ & Controller::CPU) {
            writeValueToFile("cpu.weight", "100"); // Default weight
        }
        if (controllers_ & Controller::MEMORY) {
            writeValueToFile("memory.swap.max", "0"); // Disable swap by default
        }
        if (controllers_ & Controller::PIDS) {
            writeValueToFile("pids.max", "max"); // No process limit by default
        }
    }

    void clearProcesses() {
        auto processes = getProcesses();
        for (pid_t pid : processes) {
            removeProcess(pid);
        }
    }

    std::string getCgroupParentPath() const {
        return std::filesystem::path(cgroup_path_).parent_path().string();
    }

    void validateActive() const {
        if (!active_) {
            throw std::runtime_error("Cgroup not active");
        }
    }

    void validateController(Controller controller) const {
        if (!(controllers_ & controller)) {
            throw std::runtime_error("Controller not enabled");
        }
    }

protected:
    void writeValueToFile(const std::string& filename, const std::string& value) {
        std::string file_path = cgroup_path_ + "/" + filename;
        std::ofstream file(file_path);

        if (!file) {
            throw std::runtime_error("Failed to open " + file_path);
        }

        file << value << std::endl;

        if (!file.good()) {
            throw std::runtime_error("Failed to write to " + file_path);
        }
    }

    std::string readValueFromFile(const std::string& filename) const {
        std::string file_path = cgroup_path_ + "/" + filename;
        std::ifstream file(file_path);

        if (!file) {
            throw std::runtime_error("Failed to open " + file_path);
        }

        std::string value;
        std::getline(file, value);
        return value;
    }
};
```

### 2. CPU Controller Implementation

```cpp
class CpuController : public CgroupManager {
public:
    explicit CpuController(const std::string& cgroup_path)
        : CgroupManager(cgroup_path, Controller::CPU) {}

    struct CpuLimit {
        uint64_t max_us;     // Maximum CPU time in microseconds
        uint64_t period_us;  // Period in microseconds
    };

    struct CpuStats {
        uint64_t usage_usec;      // Total CPU usage in microseconds
        uint64_t user_usec;       // User space CPU time
        uint64_t system_usec;     // System space CPU time
        uint64_t nr_periods;      // Number of periods
        uint64_t nr_throttled;    // Number of throttled periods
        uint64_t throttled_usec;  // Time throttled
    };

    void setMaxCpu(const CpuLimit& limit) {
        std::string max_value = std::to_string(limit.max_us) + " " +
                               std::to_string(limit.period_us);
        writeValueToFile("cpu.max", max_value);
    }

    void setCpuWeight(uint64_t weight) {
        if (weight < 1 || weight > 10000) {
            throw std::invalid_argument("CPU weight must be between 1 and 10000");
        }
        writeValueToFile("cpu.weight", std::to_string(weight));
    }

    void setCpuMax(double cpu_percent) {
        if (cpu_percent <= 0 || cpu_percent > 100) {
            throw std::invalid_argument("CPU percentage must be between 0 and 100");
        }

        uint64_t period_us = 100000; // 100ms default period
        uint64_t max_us = static_cast<uint64_t>(cpu_percent * period_us / 100.0);

        CpuLimit limit{max_us, period_us};
        setMaxCpu(limit);
    }

    void setCpuQuota(int64_t quota_us) {
        if (quota_us < -1) {
            throw std::invalid_argument("CPU quota must be -1 (unlimited) or positive");
        }

        uint64_t period_us = 100000; // Default period
        CpuLimit limit{static_cast<uint64_t>(quota_us), period_us};
        setMaxCpu(limit);
    }

    void setCpuPeriod(uint64_t period_us) {
        if (period_us < 1000 || period_us > 1000000) {
            throw std::invalid_argument("CPU period must be between 1ms and 1s");
        }

        // Read current max value
        std::string current_max = readValueFromFile("cpu.max");
        std::istringstream iss(current_max);
        uint64_t current_max_us, current_period_us;
        iss >> current_max_us >> current_period_us;

        // Update with new period
        CpuLimit limit{current_max_us, period_us};
        setMaxCpu(limit);
    }

    CpuStats getCpuStats() const {
        CpuStats stats{};

        // Read cpu.stat file
        std::string stat_content = readValueFromFile("cpu.stat");
        std::istringstream iss(stat_content);
        std::string line;

        while (std::getline(iss, line)) {
            size_t space_pos = line.find(' ');
            if (space_pos == std::string::npos) continue;

            std::string key = line.substr(0, space_pos);
            uint64_t value = std::stoull(line.substr(space_pos + 1));

            if (key == "usage_usec") {
                stats.usage_usec = value;
            } else if (key == "user_usec") {
                stats.user_usec = value;
            } else if (key == "system_usec") {
                stats.system_usec = value;
            } else if (key == "nr_periods") {
                stats.nr_periods = value;
            } else if (key == "nr_throttled") {
                stats.nr_throttled = value;
            } else if (key == "throttled_usec") {
                stats.throttled_usec = value;
            }
        }

        return stats;
    }

    double getCpuUsagePercent() const {
        auto stats = getCpuStats();
        uint64_t total_time = stats.user_usec + stats.system_usec;

        // Get total CPU time (system-wide)
        std::ifstream proc_stat("/proc/stat");
        std::string line;
        std::getline(proc_stat, line); // First line is aggregate CPU stats

        std::istringstream iss(line);
        std::string cpu_label;
        iss >> cpu_label;

        uint64_t user, nice, system, idle, iowait, irq, softirq, steal;
        iss >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;

        uint64_t total_system_time = user + nice + system + idle + iowait + irq + softirq + steal;

        if (total_system_time == 0) {
            return 0.0;
        }

        return (static_cast<double>(total_time) / static_cast<double>(total_system_time)) * 100.0;
    }

    void enableIdleDetection(uint64_t idle_threshold_us) {
        writeValueToFile("cpu.idle", std::to_string(idle_threshold_us));
    }

    void setBurst(uint64_t burst_us) {
        writeValueToFile("cpu.max.burst", std::to_string(burst_us));
    }
};
```

### 3. Memory Controller Implementation

```cpp
class MemoryController : public CgroupManager {
public:
    explicit MemoryController(const std::string& cgroup_path)
        : CgroupManager(cgroup_path, Controller::MEMORY) {}

    struct MemoryStats {
        uint64_t current;        // Current memory usage
        uint64_t peak;          // Peak memory usage
        uint64_t swap_current;  // Current swap usage
        uint64_t swap_peak;     // Peak swap usage
        uint64_t anon;          // Anonymous memory
        uint64_t file;          // File-backed memory
        uint64_t kernel;        // Kernel memory
        uint64_t sock;          // Socket memory
        uint64_t shmem;         // Shared memory
        uint64_t file_mapped;   // Mapped file memory
        uint64_t file_dirty;    // Dirty file memory
        uint64_t file_writeback; // Writeback memory
    };

    void setMemoryMax(uint64_t max_bytes) {
        writeValueToFile("memory.max", std::to_string(max_bytes));
    }

    void setMemoryLow(uint64_t low_bytes) {
        writeValueToFile("memory.low", std::to_string(low_bytes));
    }

    void setMemoryHigh(uint64_t high_bytes) {
        writeValueToFile("memory.high", std::to_string(high_bytes));
    }

    void setSwapMax(uint64_t max_bytes) {
        writeValueToFile("memory.swap.max", std::to_string(max_bytes));
    }

    void setSwapMaxUnlimited() {
        writeValueToFile("memory.swap.max", "max");
    }

    void setSwapMaxDisabled() {
        writeValueToFile("memory.swap.max", "0");
    }

    MemoryStats getMemoryStats() const {
        MemoryStats stats{};

        // Read memory.stat file
        std::string stat_content = readValueFromFile("memory.stat");
        std::istringstream iss(stat_content);
        std::string line;

        while (std::getline(iss, line)) {
            size_t space_pos = line.find(' ');
            if (space_pos == std::string::npos) continue;

            std::string key = line.substr(0, space_pos);
            uint64_t value = std::stoull(line.substr(space_pos + 1));

            if (key == "anon") {
                stats.anon = value;
            } else if (key == "file") {
                stats.file = value;
            } else if (key == "kernel") {
                stats.kernel = value;
            } else if (key == "sock") {
                stats.sock = value;
            } else if (key == "shmem") {
                stats.shmem = value;
            } else if (key == "file_mapped") {
                stats.file_mapped = value;
            } else if (key == "file_dirty") {
                stats.file_dirty = value;
            } else if (key == "file_writeback") {
                stats.file_writeback = value;
            }
        }

        // Read current memory usage
        stats.current = std::stoull(readValueFromFile("memory.current"));

        // Read peak memory usage
        std::string peak_str = readValueFromFile("memory.peak");
        stats.peak = std::stoull(peak_str);

        // Read swap usage
        stats.swap_current = std::stoull(readValueFromFile("memory.swap.current"));
        stats.swap_peak = std::stoull(readValueFromFile("memory.swap.peak"));

        return stats;
    }

    double getMemoryUsagePercent() const {
        auto stats = getMemoryStats();
        uint64_t max_memory = getMemoryLimit();

        if (max_memory == 0) {
            return 0.0; // Unlimited memory
        }

        return (static_cast<double>(stats.current) / static_cast<double>(max_memory)) * 100.0;
    }

    uint64_t getMemoryLimit() const {
        std::string max_str = readValueFromFile("memory.max");
        if (max_str == "max") {
            return 0; // Unlimited
        }
        return std::stoull(max_str);
    }

    void enableOOMGroupKill() {
        writeValueToFile("memory.oom.group", "1");
    }

    void disableOOMGroupKill() {
        writeValueToFile("memory.oom.group", "0");
    }

    void setMemoryPressureLevel(uint64_t level) {
        writeValueToFile("memory.pressure_level", std::to_string(level));
    }

    struct MemoryPressure {
        uint64_t avg10;  // 10-second average
        uint64_t avg60;  // 60-second average
        uint64_t avg300; // 300-second average
    };

    MemoryPressure getMemoryPressure() const {
        MemoryPressure pressure{};
        std::string pressure_content = readValueFromFile("memory.pressure");
        std::istringstream iss(pressure_content);
        std::string line;

        while (std::getline(iss, line)) {
            if (line.find("avg10=") != std::string::npos) {
                pressure.avg10 = std::stoull(line.substr(6));
            } else if (line.find("avg60=") != std::string::npos) {
                pressure.avg60 = std::stoull(line.substr(6));
            } else if (line.find("avg300=") != std::string::npos) {
                pressure.avg300 = std::stoull(line.substr(7));
            }
        }

        return pressure;
    }

    void setMemswLimit(uint64_t limit_bytes) {
        // This is a cgroup v1 feature, but we can simulate it in v2
        setMemoryMax(limit_bytes);
        setSwapMax(limit_bytes);
    }

    bool isOOMKilled() const {
        // Check if any process in this cgroup was OOM killed
        std::string oom_control = readValueFromFile("memory.events");
        return oom_control.find("oom") != std::string::npos;
    }
};
```

### 4. I/O Controller Implementation

```cpp
class IoController : public CgroupManager {
public:
    explicit IoController(const std::string& cgroup_path)
        : CgroupManager(cgroup_path, Controller::IO) {}

    struct IoLimit {
        std::string device;     // Major:minor device numbers
        uint64_t read_bps;      // Read bytes per second
        uint64_t write_bps;     // Write bytes per second
        uint64_t read_iops;     // Read I/O operations per second
        uint64_t write_iops;    // Write I/O operations per second
    };

    struct IoStats {
        uint64_t read_bytes;
        uint64_t write_bytes;
        uint64_t read_operations;
        uint64_t write_operations;
        uint64_t read_time_ms;
        uint64_t write_time_ms;
    };

    void setIoMax(const IoLimit& limit) {
        std::string max_value = limit.device + " rbps=" + std::to_string(limit.read_bps) +
                               " wbps=" + std::to_string(limit.write_bps) +
                               " riops=" + std::to_string(limit.read_iops) +
                               " wiops=" + std::to_string(limit.write_iops);

        writeValueToFile("io.max", max_value);
    }

    void setIoBps(const std::string& device, uint64_t read_bps, uint64_t write_bps) {
        std::string max_value = device + " rbps=" + std::to_string(read_bps) +
                               " wbps=" + std::to_string(write_bps);
        writeValueToFile("io.max", max_value);
    }

    void setIoIops(const std::string& device, uint64_t read_iops, uint64_t write_iops) {
        std::string max_value = device + " riops=" + std::to_string(read_iops) +
                               " wiops=" + std::to_string(write_iops);
        writeValueToFile("io.max", max_value);
    }

    void setIoWeight(uint64_t weight) {
        if (weight < 1 || weight > 10000) {
            throw std::invalid_argument("I/O weight must be between 1 and 10000");
        }
        writeValueToFile("io.weight", std::to_string(weight));
    }

    void setIoWeight(const std::string& device, uint64_t weight) {
        std::string weight_value = device + " " + std::to_string(weight);
        writeValueToFile("io.weight", weight_value);
    }

    IoStats getIoStats() const {
        IoStats stats{};
        std::string stat_content = readValueFromFile("io.stat");
        std::istringstream iss(stat_content);
        std::string line;

        while (std::getline(iss, line)) {
            // Parse device-specific stats
            size_t space_pos = line.find(' ');
            if (space_pos == std::string::npos) continue;

            std::string device = line.substr(0, space_pos);
            std::string device_stats = line.substr(space_pos + 1);

            std::istringstream device_iss(device_stats);
            std::string stat_item;

            while (device_iss >> stat_item) {
                size_t equal_pos = stat_item.find('=');
                if (equal_pos == std::string::npos) continue;

                std::string key = stat_item.substr(0, equal_pos);
                uint64_t value = std::stoull(stat_item.substr(equal_pos + 1));

                if (key == "rbytes") {
                    stats.read_bytes += value;
                } else if (key == "wbytes") {
                    stats.write_bytes += value;
                } else if (key == "rios") {
                    stats.read_operations += value;
                } else if (key == "wios") {
                    stats.write_operations += value;
                } else if (key == "rtime") {
                    stats.read_time_ms += value / 1000000; // Convert nanoseconds to milliseconds
                } else if (key == "wtime") {
                    stats.write_time_ms += value / 1000000; // Convert nanoseconds to milliseconds
                }
            }
        }

        return stats;
    }

    std::vector<std::string> getIoDevices() const {
        std::vector<std::string> devices;
        std::string stat_content = readValueFromFile("io.stat");
        std::istringstream iss(stat_content);
        std::string line;

        while (std::getline(iss, line)) {
            size_t space_pos = line.find(' ');
            if (space_pos != std::string::npos) {
                std::string device = line.substr(0, space_pos);
                devices.push_back(device);
            }
        }

        return devices;
    }

    void enableIoLatencyMonitoring() {
        writeValueToFile("io.latency", "enable");
    }

    void setIoLatency(const std::string& device, uint64_t target_latency_us) {
        std::string latency_value = device + " target=" + std::to_string(target_latency_us);
        writeValueToFile("io.latency", latency_value);
    }

    void clearIoLimits() {
        writeValueToFile("io.max", "");
    }

    bool isIoThrottled() const {
        auto stats = getIoStats();
        return (stats.read_time_ms > 0) || (stats.write_time_ms > 0);
    }
};
```

### 5. PID Controller Implementation

```cpp
class PidController : public CgroupManager {
public:
    explicit PidController(const std::string& cgroup_path)
        : CgroupManager(cgroup_path, Controller::PIDS) {}

    void setPidMax(int64_t max_pids) {
        if (max_pids < -1) {
            throw std::invalid_argument("PID max must be -1 (unlimited) or positive");
        }
        writeValueToFile("pids.max", std::to_string(max_pids));
    }

    void setPidMaxUnlimited() {
        writeValueToFile("pids.max", "max");
    }

    int64_t getPidMax() const {
        std::string max_str = readValueFromFile("pids.max");
        if (max_str == "max") {
            return -1; // Unlimited
        }
        return std::stoll(max_str);
    }

    uint64_t getCurrentPidCount() const {
        std::string current_str = readValueFromFile("pids.current");
        return std::stoull(current_str);
    }

    double getPidUsagePercent() const {
        int64_t max_pids = getPidMax();
        if (max_pids <= 0) {
            return 0.0; // Unlimited or invalid
        }

        uint64_t current_pids = getCurrentPidCount();
        return (static_cast<double>(current_pids) / static_cast<double>(max_pids)) * 100.0;
    }

    bool isPidLimitReached() const {
        int64_t max_pids = getPidMax();
        if (max_pids <= 0) {
            return false; // Unlimited
        }

        uint64_t current_pids = getCurrentPidCount();
        return current_pids >= static_cast<uint64_t>(max_pids);
    }

    struct PidEvents {
        uint64_t total;
        uint64_t max;
    };

    PidEvents getPidEvents() const {
        PidEvents events{};
        std::string events_content = readValueFromFile("pids.events");
        std::istringstream iss(events_content);
        std::string line;

        while (std::getline(iss, line)) {
            size_t space_pos = line.find(' ');
            if (space_pos == std::string::npos) continue;

            std::string key = line.substr(0, space_pos);
            uint64_t value = std::stoull(line.substr(space_pos + 1));

            if (key == "total") {
                events.total = value;
            } else if (key == "max") {
                events.max = value;
            }
        }

        return events;
    }

    bool hasPidFailures() const {
        auto events = getPidEvents();
        return events.max > 0;
    }

    void resetPidEvents() {
        // Can't directly reset events, but we can monitor changes
        // Implementation would track previous events and calculate differences
    }
};
```

## Unified Cgroup Manager

### 1. Multi-Controller Manager

```cpp
class UnifiedCgroupManager {
public:
    struct ResourceLimits {
        // CPU limits
        double cpu_percent = 0;        // 0 = unlimited
        uint64_t cpu_weight = 100;     // 1-10000

        // Memory limits
        uint64_t memory_limit = 0;     // 0 = unlimited
        uint64_t memory_swap_limit = 0; // 0 = unlimited
        uint64_t memory_low = 0;       // Low watermark

        // I/O limits
        std::vector<IoController::IoLimit> io_limits;
        uint64_t io_weight = 100;      // 1-10000

        // PID limits
        int64_t pid_limit = -1;        // -1 = unlimited
    };

    explicit UnifiedCgroupManager(const std::string& cgroup_path)
        : cgroup_path_(cgroup_path) {
        initializeControllers();
    }

    void create() {
        // Create base cgroup
        std::filesystem::create_directories(cgroup_path_);

        // Initialize all controllers
        if (cpu_controller_) cpu_controller_->create();
        if (memory_controller_) memory_controller_->create();
        if (io_controller_) io_controller_->create();
        if (pid_controller_) pid_controller_->create();

        active_ = true;
    }

    void destroy() {
        if (!active_) return;

        if (cpu_controller_) cpu_controller_->destroy();
        if (memory_controller_) memory_controller_->destroy();
        if (io_controller_) io_controller_->destroy();
        if (pid_controller_) pid_controller_->destroy();

        std::filesystem::remove_all(cgroup_path_);
        active_ = false;
    }

    void setResourceLimits(const ResourceLimits& limits) {
        validateActive();

        // Apply CPU limits
        if (cpu_controller_ && limits.cpu_percent > 0) {
            cpu_controller_->setCpuMax(limits.cpu_percent);
        }
        if (cpu_controller_ && limits.cpu_weight != 100) {
            cpu_controller_->setCpuWeight(limits.cpu_weight);
        }

        // Apply memory limits
        if (memory_controller_) {
            if (limits.memory_limit > 0) {
                memory_controller_->setMemoryMax(limits.memory_limit);
            }
            if (limits.memory_swap_limit > 0) {
                memory_controller_->setSwapMax(limits.memory_swap_limit);
            }
            if (limits.memory_low > 0) {
                memory_controller_->setMemoryLow(limits.memory_low);
            }
        }

        // Apply I/O limits
        if (io_controller_) {
            if (!limits.io_limits.empty()) {
                for (const auto& limit : limits.io_limits) {
                    io_controller_->setIoMax(limit);
                }
            }
            if (limits.io_weight != 100) {
                io_controller_->setIoWeight(limits.io_weight);
            }
        }

        // Apply PID limits
        if (pid_controller_ && limits.pid_limit != -1) {
            pid_controller_->setPidMax(limits.pid_limit);
        }
    }

    void addProcess(pid_t pid) {
        validateActive();

        // Add process to the unified cgroup
        std::string cgroup_procs = cgroup_path_ + "/cgroup.procs";
        std::ofstream procs_file(cgroup_procs, std::ios::app);

        if (!procs_file) {
            throw std::runtime_error("Failed to add process to cgroup");
        }

        procs_file << pid << std::endl;
    }

    void removeProcess(pid_t pid) {
        validateActive();

        // Move process to parent cgroup
        std::string parent_procs = std::filesystem::path(cgroup_path_).parent_path().string() + "/cgroup.procs";
        std::ofstream parent_file(parent_procs, std::ios::app);

        if (parent_file) {
            parent_file << pid << std::endl;
        }
    }

    struct ResourceUsage {
        double cpu_usage_percent;
        uint64_t memory_usage_bytes;
        double memory_usage_percent;
        IoController::IoStats io_stats;
        uint64_t current_pids;
        double pid_usage_percent;
    };

    ResourceUsage getResourceUsage() const {
        validateActive();

        ResourceUsage usage{};

        if (cpu_controller_) {
            usage.cpu_usage_percent = cpu_controller_->getCpuUsagePercent();
        }

        if (memory_controller_) {
            usage.memory_usage_bytes = memory_controller_->getMemoryStats().current;
            usage.memory_usage_percent = memory_controller_->getMemoryUsagePercent();
        }

        if (io_controller_) {
            usage.io_stats = io_controller_->getIoStats();
        }

        if (pid_controller_) {
            usage.current_pids = pid_controller_->getCurrentPidCount();
            usage.pid_usage_percent = pid_controller_->getPidUsagePercent();
        }

        return usage;
    }

    void freeze() {
        validateActive();

        if (cpu_controller_) {
            cpu_controller_->freeze();
        }
    }

    void thaw() {
        validateActive();

        if (cpu_controller_) {
            cpu_controller_->thaw();
        }
    }

    bool isFrozen() const {
        validateActive();

        if (cpu_controller_) {
            return cpu_controller_->isFrozen();
        }

        return false;
    }

    // Access to individual controllers
    CpuController* getCpuController() const { return cpu_controller_.get(); }
    MemoryController* getMemoryController() const { return memory_controller_.get(); }
    IoController* getIoController() const { return io_controller_.get(); }
    PidController* getPidController() const { return pid_controller_.get(); }

private:
    std::string cgroup_path_;
    std::unique_ptr<CpuController> cpu_controller_;
    std::unique_ptr<MemoryController> memory_controller_;
    std::unique_ptr<IoController> io_controller_;
    std::unique_ptr<PidController> pid_controller_;
    bool active_ = false;

    void initializeControllers() {
        // Check which controllers are available
        std::string controllers_file = cgroup_path_ + "/cgroup.controllers";
        if (std::filesystem::exists(controllers_file)) {
            std::ifstream file(controllers_file);
            std::string controllers_content;
            std::getline(file, controllers_content);

            if (controllers_content.find("cpu") != std::string::npos) {
                cpu_controller_ = std::make_unique<CpuController>(cgroup_path_);
            }
            if (controllers_content.find("memory") != std::string::npos) {
                memory_controller_ = std::make_unique<MemoryController>(cgroup_path_);
            }
            if (controllers_content.find("io") != std::string::npos) {
                io_controller_ = std::make_unique<IoController>(cgroup_path_);
            }
            if (controllers_content.find("pids") != std::string::npos) {
                pid_controller_ = std::make_unique<PidController>(cgroup_path_);
            }
        }
    }

    void validateActive() const {
        if (!active_) {
            throw std::runtime_error("Cgroup manager not active");
        }
    }
};
```

## Container Integration

### 1. Container Cgroup Manager

```cpp
class ContainerCgroupManager {
public:
    explicit ContainerCgroupManager(const std::string& container_id)
        : container_id_(container_id) {
        cgroup_path_ = "/sys/fs/cgroup/docker-cpp/" + container_id_;
        cgroup_manager_ = std::make_unique<UnifiedCgroupManager>(cgroup_path_);
    }

    void initialize(const UnifiedCgroupManager::ResourceLimits& limits) {
        cgroup_manager_->create();
        cgroup_manager_->setResourceLimits(limits);
    }

    void attachContainerProcess(pid_t pid) {
        cgroup_manager_->addProcess(pid);
        container_pid_ = pid;
    }

    void updateLimits(const UnifiedCgroupManager::ResourceLimits& limits) {
        cgroup_manager_->setResourceLimits(limits);
    }

    void stop() {
        if (container_pid_ > 0) {
            // Freeze before stopping
            cgroup_manager_->freeze();

            // Send SIGTERM
            kill(container_pid_, SIGTERM);

            // Wait for graceful shutdown
            int status;
            int timeout = 10;
            pid_t result;

            while (timeout-- > 0) {
                result = waitpid(container_pid_, &status, WNOHANG);
                if (result > 0) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }

            // Force kill if still running
            if (result == 0) {
                kill(container_pid_, SIGKILL);
                waitpid(container_pid_, &status, 0);
            }
        }

        cgroup_manager_->destroy();
    }

    UnifiedCgroupManager::ResourceUsage getUsage() const {
        return cgroup_manager_->getResourceUsage();
    }

    bool isAlive() const {
        if (container_pid_ <= 0) {
            return false;
        }

        return kill(container_pid_, 0) == 0;
    }

    void freeze() {
        cgroup_manager_->freeze();
    }

    void thaw() {
        cgroup_manager_->thaw();
    }

private:
    std::string container_id_;
    std::string cgroup_path_;
    std::unique_ptr<UnifiedCgroupManager> cgroup_manager_;
    pid_t container_pid_ = -1;
};
```

## Performance Monitoring

### 1. Cgroup Monitor

```cpp
class CgroupMonitor {
public:
    struct MonitoringData {
        std::chrono::system_clock::time_point timestamp;
        UnifiedCgroupManager::ResourceUsage usage;
        bool is_frozen;
        bool is_alive;
    };

    explicit CgroupMonitor(std::shared_ptr<ContainerCgroupManager> manager)
        : manager_(std::move(manager)), monitoring_(false) {}

    void startMonitoring(std::chrono::milliseconds interval = std::chrono::milliseconds(1000)) {
        if (monitoring_) {
            throw std::runtime_error("Monitoring already started");
        }

        monitoring_ = true;
        monitor_thread_ = std::thread([this, interval]() {
            monitorLoop(interval);
        });
    }

    void stopMonitoring() {
        monitoring_ = false;
        if (monitor_thread_.joinable()) {
            monitor_thread_.join();
        }
    }

    std::vector<MonitoringData> getMonitoringData() const {
        std::lock_guard lock(data_mutex_);
        return monitoring_data_;
    }

    std::optional<MonitoringData> getLatestData() const {
        std::lock_guard lock(data_mutex_);
        if (monitoring_data_.empty()) {
            return std::nullopt;
        }
        return monitoring_data_.back();
    }

    void clearMonitoringData() {
        std::lock_guard lock(data_mutex_);
        monitoring_data_.clear();
    }

private:
    std::shared_ptr<ContainerCgroupManager> manager_;
    std::atomic<bool> monitoring_;
    std::thread monitor_thread_;
    std::vector<MonitoringData> monitoring_data_;
    mutable std::mutex data_mutex_;

    void monitorLoop(std::chrono::milliseconds interval) {
        while (monitoring_) {
            try {
                MonitoringData data;
                data.timestamp = std::chrono::system_clock::now();
                data.usage = manager_->getUsage();
                data.is_frozen = false; // Would need to check this
                data.is_alive = manager_->isAlive();

                {
                    std::lock_guard lock(data_mutex_);
                    monitoring_data_.push_back(data);

                    // Keep only last 1000 data points
                    if (monitoring_data_.size() > 1000) {
                        monitoring_data_.erase(monitoring_data_.begin(),
                                             monitoring_data_.begin() + monitoring_data_.size() - 1000);
                    }
                }

            } catch (const std::exception& e) {
                // Log error but continue monitoring
                std::cerr << "Monitoring error: " << e.what() << std::endl;
            }

            std::this_thread::sleep_for(interval);
        }
    }
};
```

## Usage Example

```cpp
int main() {
    try {
        // Create container cgroup manager
        auto container_manager = std::make_shared<ContainerCgroupManager>("test_container");

        // Set resource limits
        UnifiedCgroupManager::ResourceLimits limits;
        limits.cpu_percent = 50.0;        // 50% CPU
        limits.memory_limit = 512 * 1024 * 1024; // 512MB memory
        limits.pid_limit = 100;           // Max 100 processes

        // Initialize container cgroup
        container_manager->initialize(limits);

        // Create a process (simplified)
        pid_t pid = fork();
        if (pid == 0) {
            // Child process - run some workload
            execl("/bin/stress", "stress", "--cpu", "1", "--timeout", "30", nullptr);
            _exit(1);
        }

        // Attach process to cgroup
        container_manager->attachContainerProcess(pid);

        // Start monitoring
        CgroupMonitor monitor(container_manager);
        monitor.startMonitoring(std::chrono::milliseconds(500));

        // Let it run for 10 seconds
        std::this_thread::sleep_for(std::chrono::seconds(10));

        // Get monitoring data
        auto data = monitor.getMonitoringData();
        std::cout << "Collected " << data.size() << " data points" << std::endl;

        if (auto latest = monitor.getLatestData()) {
            std::cout << "Latest CPU usage: " << latest->usage.cpu_usage_percent << "%" << std::endl;
            std::cout << "Latest memory usage: " << latest->usage.memory_usage_bytes << " bytes" << std::endl;
        }

        // Stop monitoring
        monitor.stopMonitoring();

        // Stop container
        container_manager->stop();

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
```

## Conclusion

The cgroups management system presented in this article provides comprehensive resource control capabilities for container runtimes. Key features include:

1. **Full cgroup v2 Support**: Modern unified hierarchy with all major controllers
2. **Fine-Grained Control**: Precise resource limits and monitoring
3. **Performance Efficient**: Minimal overhead with optimized operations
4. **Container Integration**: Seamless integration with container lifecycle
5. **Real-time Monitoring**: Continuous resource usage tracking

This implementation forms the resource control foundation of our docker-cpp project, enabling efficient and fair resource allocation among containers while providing the isolation necessary for multi-tenant environments.

## Next Steps

In our next article, "Image Management System," we'll explore how to implement container image storage, distribution, and management, building on the resource control capabilities established here.

---

**Previous Article**: [Process Isolation: Implementing Linux Namespaces in C++](./06-process-isolation-namespaces.md)
**Next Article**: [Image Management System](./08-image-management-system.md)
**Series Index**: [Table of Contents](./00-table-of-contents.md)
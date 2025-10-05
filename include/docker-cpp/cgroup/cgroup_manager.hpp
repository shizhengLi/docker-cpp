#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace docker_cpp {

// Forward declarations
class CgroupManager;
class ResourceMonitor;

// Cgroup controller types (cgroup v2 unified hierarchy)
enum class CgroupController : uint32_t {
    CPU = 0x01,
    MEMORY = 0x02,
    IO = 0x04,
    PID = 0x08,
    CPUSET = 0x10,
    HUGETLB = 0x20,
    RDMA = 0x40,
    MISC = 0x80,
    ALL = 0xFFFFFFFF
};

// Cgroup statistics structures
struct CpuStats {
    uint64_t usage_usec;     // CPU usage in microseconds
    uint64_t user_usec;      // User mode CPU usage
    uint64_t system_usec;    // System mode CPU usage
    uint64_t nr_periods;     // Number of enforcement intervals
    uint64_t nr_throttled;   // Number of throttled intervals
    uint64_t throttled_usec; // Total time throttled
    double usage_percent;    // CPU usage percentage
};

struct MemoryStats {
    uint64_t current;      // Current memory usage in bytes
    uint64_t peak;         // Peak memory usage in bytes
    uint64_t limit;        // Memory limit in bytes
    uint64_t swap_current; // Current swap usage in bytes
    uint64_t swap_peak;    // Peak swap usage in bytes
    uint64_t swap_limit;   // Swap limit in bytes
    uint64_t anon;         // Anonymous memory usage
    uint64_t file;         // File-backed memory usage
    uint64_t kernel_stack; // Kernel stack usage
    uint64_t slab;         // Slab allocator usage
    uint64_t sock;         // Sockets memory usage
    uint64_t file_mapped;  // Mapped file usage
    uint64_t shmem;        // Shared memory usage
    double usage_percent;  // Memory usage percentage
};

struct IoStats {
    uint64_t rbytes; // Bytes read
    uint64_t wbytes; // Bytes written
    uint64_t rios;   // Read I/O operations
    uint64_t wios;   // Write I/O operations
    uint64_t dbytes; // Bytes discarded
    uint64_t dios;   // Discard I/O operations
};

struct PidStats {
    uint64_t current; // Current number of processes
    uint64_t max;     // Maximum number of processes
};

// Resource metrics container
struct ResourceMetrics {
    CpuStats cpu;
    MemoryStats memory;
    IoStats io;
    PidStats pid;
    uint64_t timestamp; // Timestamp of metrics collection
};

// Cgroup configuration
struct CgroupConfig {
    std::string name;             // Cgroup name/path
    CgroupController controllers; // Enabled controllers
    std::string parent_path;      // Parent cgroup path (empty for root)

    // CPU configuration
    struct {
        uint64_t max_usec;    // CPU max in microseconds (0 = unlimited)
        uint64_t period_usec; // CPU period in microseconds
        uint64_t weight;      // CPU weight (1-10000)
        uint64_t burst_usec;  // CPU burst in microseconds
    } cpu;

    // Memory configuration
    struct {
        uint64_t max_bytes;      // Memory limit in bytes (0 = unlimited)
        uint64_t swap_max_bytes; // Swap limit in bytes (0 = unlimited)
        uint64_t low_bytes;      // Memory low threshold in bytes
        uint64_t high_bytes;     // Memory high threshold in bytes
        bool oom_kill_enable;    // Enable OOM killer
    } memory;

    // IO configuration
    struct {
        uint64_t read_bps;   // Read bytes per second
        uint64_t write_bps;  // Write bytes per second
        uint64_t read_iops;  // Read I/O operations per second
        uint64_t write_iops; // Write I/O operations per second
    } io;

    // PID configuration
    struct {
        uint64_t max; // Maximum number of processes
    } pid;

    // Constructor with defaults
    CgroupConfig();
};

// Cgroup manager interface
class CgroupManager {
public:
    // Factory method to create cgroup manager
    static std::unique_ptr<CgroupManager> create(const CgroupConfig& config);

    virtual ~CgroupManager() = default;

    // Lifecycle management
    virtual void create() = 0;
    virtual void destroy() = 0;
    virtual bool exists() const = 0;

    // Process management
    virtual void addProcess(pid_t pid) = 0;
    virtual void removeProcess(pid_t pid) = 0;
    virtual std::vector<pid_t> getProcesses() const = 0;

    // Controller operations
    virtual void enableController(CgroupController controller) = 0;
    virtual void disableController(CgroupController controller) = 0;
    virtual bool isControllerEnabled(CgroupController controller) const = 0;

    // Resource limits and settings
    virtual void setCpuMax(uint64_t max_usec, uint64_t period_usec) = 0;
    virtual void setCpuWeight(uint64_t weight) = 0;
    virtual void setCpuBurst(uint64_t burst_usec) = 0;

    virtual void setMemoryMax(uint64_t max_bytes) = 0;
    virtual void setMemorySwapMax(uint64_t max_bytes) = 0;
    virtual void setMemoryLow(uint64_t low_bytes) = 0;
    virtual void setMemoryHigh(uint64_t high_bytes) = 0;
    virtual void setOomKillEnable(bool enable) = 0;

    virtual void setIoMax(const std::string& device, uint64_t read_bps, uint64_t write_bps) = 0;
    virtual void setIoBps(const std::string& device, uint64_t read_bps, uint64_t write_bps) = 0;
    virtual void setIoIops(const std::string& device, uint64_t read_iops, uint64_t write_iops) = 0;

    virtual void setPidMax(uint64_t max) = 0;

    // Statistics and monitoring
    virtual ResourceMetrics getMetrics() const = 0;
    virtual CpuStats getCpuStats() const = 0;
    virtual MemoryStats getMemoryStats() const = 0;
    virtual IoStats getIoStats() const = 0;
    virtual PidStats getPidStats() const = 0;

    // Configuration and state
    virtual std::string getPath() const = 0;
    virtual CgroupConfig getConfig() const = 0;
    virtual void updateConfig(const CgroupConfig& config) = 0;

    // Notifications and events
    virtual void enableMemoryPressureEvents() = 0;
    virtual void enableOomEvents() = 0;
    virtual bool hasMemoryPressureEvent() const = 0;
    virtual bool hasOomEvent() const = 0;

    // Utility methods
    static bool isCgroupV2Supported();
    static std::string getMountPoint();
    static std::vector<std::string> listControllers();
    static bool isControllerAvailable(const std::string& controller);

protected:
    CgroupManager() = default;

private:
    // Non-copyable
    CgroupManager(const CgroupManager&) = delete;
    CgroupManager& operator=(const CgroupManager&) = delete;
};

// Resource monitor interface
class ResourceMonitor {
public:
    // Factory method
    static std::unique_ptr<ResourceMonitor> create();

    virtual ~ResourceMonitor() = default;

    // Monitoring control
    virtual void startMonitoring(const std::string& cgroup_path) = 0;
    virtual void stopMonitoring(const std::string& cgroup_path) = 0;
    virtual bool isMonitoring(const std::string& cgroup_path) const = 0;

    // Metrics collection
    virtual ResourceMetrics getCurrentMetrics(const std::string& cgroup_path) = 0;
    virtual std::vector<ResourceMetrics> getHistoricalMetrics(const std::string& cgroup_path,
                                                              uint64_t start_time,
                                                              uint64_t end_time) = 0;

    // Alerting
    virtual void setCpuThreshold(const std::string& cgroup_path, double threshold_percent) = 0;
    virtual void setMemoryThreshold(const std::string& cgroup_path, double threshold_percent) = 0;
    virtual void setIoThreshold(const std::string& cgroup_path, uint64_t bytes_per_second) = 0;

    virtual bool hasCpuAlert(const std::string& cgroup_path) const = 0;
    virtual bool hasMemoryAlert(const std::string& cgroup_path) const = 0;
    virtual bool hasIoAlert(const std::string& cgroup_path) const = 0;

    // Callbacks for events
    using AlertCallback = std::function<
        void(const std::string& cgroup_path, const std::string& alert_type, double value)>;
    virtual void setAlertCallback(AlertCallback callback) = 0;

protected:
    ResourceMonitor() = default;

private:
    // Non-copyable
    ResourceMonitor(const ResourceMonitor&) = delete;
    ResourceMonitor& operator=(const ResourceMonitor&) = delete;
};

// Utility functions
CgroupController operator|(CgroupController a, CgroupController b);
CgroupController operator&(CgroupController a, CgroupController b);
bool hasController(CgroupController controllers, CgroupController controller);

// String conversion utilities
std::string cgroupControllerToString(CgroupController controller);
CgroupController stringToCgroupController(const std::string& str);

// Error handling
class CgroupError : public std::exception {
public:
    enum class Code {
        SUCCESS,
        NOT_SUPPORTED,
        NOT_FOUND,
        PERMISSION_DENIED,
        INVALID_ARGUMENT,
        IO_ERROR,
        CONTROLLER_NOT_AVAILABLE,
        PROCESS_NOT_FOUND,
        MEMORY_PRESSURE,
        OOM_EVENT
    };

    CgroupError(Code code, const std::string& message);
    const char* what() const noexcept override;
    Code getCode() const;

private:
    Code code_;
    std::string message_;
};

} // namespace docker_cpp
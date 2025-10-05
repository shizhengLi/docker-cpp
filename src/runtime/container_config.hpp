#pragma once

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace dockercpp {
namespace core {
class Logger;
class EventManager;
} // namespace core

namespace plugin {
class PluginRegistry;
} // namespace plugin

namespace runtime {

// Exception types for container operations
class ContainerRuntimeError : public std::exception {
public:
    explicit ContainerRuntimeError(const std::string& message) : message_(message) {}
    const char* what() const noexcept override { return message_.c_str(); }
private:
    std::string message_;
};

class ContainerNotFoundError : public ContainerRuntimeError {
public:
    explicit ContainerNotFoundError(const std::string& id)
        : ContainerRuntimeError("Container not found: " + id) {}
};

class ContainerConfigurationError : public ContainerRuntimeError {
public:
    explicit ContainerConfigurationError(const std::string& message)
        : ContainerRuntimeError("Container configuration error: " + message) {}
};


// Container state enumeration
enum class ContainerState {
    CREATED,
    STARTING,
    RUNNING,
    PAUSED,
    STOPPING,
    STOPPED,
    REMOVING,
    REMOVED,
    DEAD,
    RESTARTING,
    ERROR
};

// Convert container state to string
std::string containerStateToString(ContainerState state);
ContainerState stringToContainerState(const std::string& state_str);

class InvalidContainerStateError : public ContainerRuntimeError {
public:
    InvalidContainerStateError(const std::string& container_id, ContainerState current, ContainerState target)
        : ContainerRuntimeError("Invalid state transition for container " + container_id +
                               " from " + containerStateToString(current) +
                               " to " + containerStateToString(target)) {}
};

// Block I/O device limit
struct BlkioDeviceLimit {
    std::string device;  // device path (e.g., "/dev/sda")
    uint64_t read_bps;   // read bytes per second
    uint64_t write_bps;  // write bytes per second
    uint64_t read_iops;  // read I/O operations per second
    uint64_t write_iops; // write I/O operations per second
};

// Resource limits structure
struct ResourceLimits {
    // Memory limits
    size_t memory_limit = 0;       // bytes, 0 = unlimited
    size_t memory_swap_limit = 0;  // bytes, 0 = unlimited
    size_t memory_reservation = 0; // bytes, soft limit

    // CPU limits
    double cpu_shares = 1.0;       // relative CPU weight (default: 1.0)
    size_t cpu_period = 100000;    // microseconds (default: 100ms)
    size_t cpu_quota = 0;          // microseconds (0 = unlimited)
    std::vector<std::string> cpus; // CPU pinning (e.g., ["0", "1", "2"])

    // Process limits
    size_t pids_limit = 0; // process count limit (0 = unlimited)

    // Block I/O limits
    uint64_t blkio_weight = 0; // block IO weight (10-1000)
    std::vector<BlkioDeviceLimit> blkio_device_limits;

    // Network limits
    uint64_t network_priority = 0; // network priority (0 = default)

    // OOM handling
    bool oom_kill_disable = false; // disable OOM killer
    int oom_score_adj = 0;         // OOM score adjustment
};

// Port mapping
struct PortMapping {
    std::string container_ip; // container IP (optional)
    uint16_t container_port;  // container port
    std::string host_ip;      // host IP (optional, default: "0.0.0.0")
    uint16_t host_port;       // host port
    std::string protocol;     // protocol ("tcp" or "udp")
    bool host_ip_empty() const
    {
        return host_ip.empty();
    }
};

// Volume mount
struct VolumeMount {
    std::string source;                        // source path (host)
    std::string destination;                   // destination path (container)
    std::string type;                          // mount type ("bind", "volume", "tmpfs")
    bool read_only = false;                    // mount as read-only
    std::string propagation;                   // mount propagation
    std::map<std::string, std::string> labels; // mount labels
    bool no_copy = false;                      // don't copy files from host to container
};

// Security configuration
struct SecurityConfig {
    std::vector<std::string> capabilities;      // Linux capabilities to add
    std::vector<std::string> drop_capabilities; // Linux capabilities to drop
    std::string seccomp_profile;                // seccomp filter profile path
    std::string apparmor_profile;               // AppArmor profile name
    std::string selinux_context;                // SELinux context
    bool read_only_rootfs = false;              // mount root filesystem as read-only
    bool no_new_privileges = true;              // disable setuid/setgid binaries
    std::string user;                           // run as user (uid:gid or username)
    std::vector<std::string> groups;            // supplementary groups
    std::string umask = "0022";                 // file creation mask
    std::vector<std::string> masked_paths;      // paths to hide inside container
    std::vector<std::string> readonly_paths;    // paths to mount as read-only
};

// Network configuration
struct NetworkConfig {
    std::string network_id;                 // network to connect to
    std::vector<std::string> aliases;       // network aliases
    std::vector<PortMapping> port_mappings; // port mappings
    std::string mac_address;                // MAC address
    std::map<std::string, std::string> dns; // DNS servers
    std::vector<std::string> dns_search;    // DNS search domains
    std::vector<std::string> extra_hosts;   // extra hosts (/etc/hosts entries)
};

// Storage configuration
struct StorageConfig {
    std::string image_id;                   // base image ID
    std::vector<VolumeMount> volume_mounts; // volume mounts
    std::string work_dir;                   // working directory inside container
    std::vector<std::string> rootfs;        // root filesystem layers
    std::string storage_driver;             // storage driver (overlay2, aufs, etc.)
};

// Health check configuration
struct HealthCheckConfig {
    std::vector<std::string> test; // command to run
    int interval = 30;             // seconds between checks
    int timeout = 30;              // timeout for each check
    int retries = 3;               // consecutive failures needed
    int start_period = 0;          // start period before counting retries
    std::string start_interval;    // interval during start period
};

// Restart policy
enum class RestartPolicy {
    NO,            // don't restart
    ON_FAILURE,    // restart on failure
    ALWAYS,        // always restart
    UNLESS_STOPPED // restart unless stopped
};

struct RestartPolicyConfig {
    RestartPolicy policy = RestartPolicy::NO;
    int max_retries = 0; // maximum retry attempts
    int timeout = 10;    // timeout between retries in seconds
};

// Main container configuration
struct ContainerConfig {
    // Basic container information
    std::string id;                   // container ID (generated)
    std::string name;                 // container name
    std::string image;                // image name or ID
    std::vector<std::string> command; // entrypoint command
    std::vector<std::string> args;    // command arguments
    std::vector<std::string> env;     // environment variables
    std::string working_dir;          // working directory

    // Interactive settings
    bool interactive = false;  // interactive container
    bool tty = false;          // allocate TTY
    bool attach_stdin = false; // attach stdin
    bool attach_stdout = true; // attach stdout
    bool attach_stderr = true; // attach stderr

    // Resource and security settings
    ResourceLimits resources;
    SecurityConfig security;

    // Network and storage settings
    NetworkConfig network;
    StorageConfig storage;

    // Health and restart settings
    HealthCheckConfig healthcheck;
    RestartPolicyConfig restart_policy;

    // Labels and annotations
    std::map<std::string, std::string> labels;
    std::map<std::string, std::string> annotations;

    // Logging configuration
    std::map<std::string, std::string> log_config;

    // Timestamps
    std::chrono::system_clock::time_point created;

    // Constructor with default values
    ContainerConfig() : created(std::chrono::system_clock::now()) {}

    // Validation methods
    bool isValid() const;
    std::vector<std::string> validate() const;

    // Helper methods
    std::string getEnvironment(const std::string& key) const;
    void setEnvironment(const std::string& key, const std::string& value);
    bool hasLabel(const std::string& key) const;
    std::string getLabel(const std::string& key) const;
};

// Resource statistics
struct ResourceStats {
    // CPU statistics
    double cpu_usage_percent = 0.0;
    uint64_t cpu_time_nanos = 0;
    uint64_t system_cpu_time_nanos = 0;

    // Memory statistics
    size_t memory_usage_bytes = 0;
    size_t memory_limit_bytes = 0;
    size_t memory_cache_bytes = 0;
    size_t memory_swap_usage_bytes = 0;
    size_t memory_swap_limit_bytes = 0;

    // PIDs statistics
    size_t current_pids = 0;
    size_t pids_limit = 0;

    // Block I/O statistics
    uint64_t blkio_read_bytes = 0;
    uint64_t blkio_write_bytes = 0;
    uint64_t blkio_read_operations = 0;
    uint64_t blkio_write_operations = 0;

    // Network statistics
    uint64_t network_rx_bytes = 0;
    uint64_t network_tx_bytes = 0;
    uint64_t network_rx_packets = 0;
    uint64_t network_tx_packets = 0;
    uint64_t network_rx_errors = 0;
    uint64_t network_tx_errors = 0;

    // Timestamp
    std::chrono::system_clock::time_point timestamp;

    ResourceStats() : timestamp(std::chrono::system_clock::now()) {}
};

// Container information (runtime state)
struct ContainerInfo {
    std::string id;
    std::string name;
    std::string image;
    ContainerState state;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point started_at;
    std::chrono::system_clock::time_point finished_at;
    pid_t pid = 0;
    int exit_code = 0;
    std::string error;
    ContainerConfig config;

    // Runtime statistics
    ResourceStats stats;

    // Network information
    std::vector<std::string> networks;
    std::map<std::string, std::string> network_settings;

    // Mount information
    std::vector<VolumeMount> mounts;

    // Check if container is running
    bool isRunning() const
    {
        return state == ContainerState::RUNNING;
    }

    // Get container uptime
    std::chrono::seconds getUptime() const;
};

// Utility functions
std::string generateContainerId();
std::string generateContainerName(const std::string& prefix);
bool isValidContainerName(const std::string& name);
bool isValidContainerId(const std::string& id);

} // namespace runtime
} // namespace dockercpp
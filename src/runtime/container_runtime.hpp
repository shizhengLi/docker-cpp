#pragma once

#include <functional>
#include <future>
#include <memory>
#include <string>
#include <vector>
#include "container.hpp"

namespace docker_cpp {
namespace core {
class Logger;
class ConfigManager;
class EventManager;
} // namespace core

namespace plugin {
class PluginRegistry;
} // namespace plugin

namespace runtime {

// Container Runtime class - main interface for container management
class ContainerRuntime {
public:
    // Constructor
    ContainerRuntime();

    // Destructor
    ~ContainerRuntime();

    // Non-copyable, movable
    ContainerRuntime(const ContainerRuntime&) = delete;
    ContainerRuntime& operator=(const ContainerRuntime&) = delete;
    ContainerRuntime(ContainerRuntime&& other) noexcept;
    ContainerRuntime& operator=(ContainerRuntime&& other) noexcept;

    // Container lifecycle operations
    std::string createContainer(const ContainerConfig& config);
    void startContainer(const std::string& container_id);
    void stopContainer(const std::string& container_id, int timeout = 10);
    void pauseContainer(const std::string& container_id);
    void resumeContainer(const std::string& container_id);
    void restartContainer(const std::string& container_id, int timeout = 10);
    void removeContainer(const std::string& container_id, bool force = false);
    void killContainer(const std::string& container_id, int signal = SIGTERM);

    // Container information
    ContainerInfo inspectContainer(const std::string& container_id) const;
    std::vector<ContainerInfo> listContainers(bool all = false) const;
    std::vector<std::string> listContainerIds(bool all = false) const;
    size_t getContainerCount() const;
    size_t getRunningContainerCount() const;

    // Container state management
    ContainerState getContainerState(const std::string& container_id) const;
    void waitForContainer(const std::string& container_id,
                          ContainerState desired_state,
                          int timeout_seconds = 60);
    std::future<void> waitForContainerAsync(const std::string& container_id,
                                            ContainerState desired_state,
                                            int timeout_seconds = 60);

    // Resource management
    void updateContainerResources(const std::string& container_id, const ResourceLimits& limits);
    ResourceStats getContainerStats(const std::string& container_id) const;
    std::vector<ResourceStats> getAllContainerStats() const;
    ResourceStats getAggregatedStats() const;

    // Container logs
    std::vector<std::string> getContainerLogs(const std::string& container_id,
                                              int tail_lines = -1,
                                              bool follow = false) const;
    void streamContainerLogs(const std::string& container_id,
                             std::function<void(const std::string&)> log_callback) const;

    // Container exec
    std::string execInContainer(const std::string& container_id,
                                const std::vector<std::string>& command,
                                const std::vector<std::string>& env = {},
                                bool tty = false,
                                bool stdin_open = false);

    // Container filesystem operations
    std::vector<std::string> getContainerFileChanges(const std::string& container_id) const;
    void exportContainer(const std::string& container_id, const std::string& output_path) const;
    std::string commitContainer(const std::string& container_id,
                                const std::string& repository,
                                const std::string& tag = "latest",
                                const std::map<std::string, std::string>& labels = {}) const;

    // System information
    struct SystemInfo {
        size_t total_containers;
        size_t running_containers;
        size_t paused_containers;
        size_t stopped_containers;
        ResourceStats system_stats;
        std::string version;
        std::string kernel_version;
        std::string operating_system;
        std::chrono::system_clock::time_point system_time;
    };

    SystemInfo getSystemInfo() const;

    // Runtime configuration
    struct RuntimeConfig {
        std::string default_runtime = "runc";
        std::string cgroup_driver = "systemd";
        std::string storage_driver = "overlay2";
        std::string log_driver = "json-file";
        std::map<std::string, std::string> log_options;
        size_t default_memory_limit = 0; // 0 = unlimited
        double default_cpu_shares = 1.0;
        bool enable_user_namespace = false;
        bool enable_cgroup_namespace = false;
        std::string default_network = "bridge";
    };

    void setRuntimeConfig(const RuntimeConfig& config);
    RuntimeConfig getRuntimeConfig() const;

    // Event handling
    using RuntimeEventCallback =
        std::function<void(const std::string&, const std::map<std::string, std::string>&)>;
    void subscribeToEvents(RuntimeEventCallback callback,
                           const std::vector<std::string>& event_types = {});
    void unsubscribeFromEvents();

    // Global operations
    void pauseAllContainers();
    void resumeAllContainers();
    void stopAllContainers(int timeout = 10);
    void removeStoppedContainers();
    void cleanupResources();

    // Runtime health and maintenance
    bool isHealthy() const;
    std::vector<std::string> getHealthChecks() const;
    void performMaintenance();

    // Shutdown
    void shutdown();

private:
    // Core components
    std::unique_ptr<ContainerRegistry> container_registry_;
    std::unique_ptr<core::ConfigManager> config_manager_;
    core::Logger* logger_;
    core::EventManager* event_manager_;
    std::unique_ptr<plugin::PluginRegistry> plugin_registry_;

    // Runtime configuration
    RuntimeConfig runtime_config_;
    mutable std::mutex config_mutex_;

    // Event handling
    std::vector<std::pair<std::vector<std::string>, RuntimeEventCallback>> event_callbacks_;
    mutable std::mutex callbacks_mutex_;

    // State
    std::atomic<bool> initialized_;
    std::atomic<bool> shutting_down_;

    // Maintenance
    std::unique_ptr<std::thread> maintenance_thread_;
    std::atomic<bool> maintenance_active_;
    std::chrono::seconds maintenance_interval_;

    // Private methods
    void initialize();
    void initializeComponents();
    void loadPlugins();
    void startMaintenanceThread();
    void stopMaintenanceThread();
    void performMaintenanceTasks();
    void validateRuntimeConfig(const RuntimeConfig& config) const;
    void emitRuntimeEvent(const std::string& event_type,
                          const std::map<std::string, std::string>& data) const;
    void logInfo(const std::string& message) const;
    void logError(const std::string& message) const;
    void logWarning(const std::string& message) const;

    // Container validation
    void validateContainerConfig(const ContainerConfig& config) const;
    void validateContainerId(const std::string& id) const;
    void validateContainerOperation(const std::string& id, const std::string& operation) const;

    // Resource monitoring
    void startResourceMonitoring();
    void stopResourceMonitoring();

    // Helper methods
    std::shared_ptr<Container> getContainer(const std::string& id) const;
    std::string generateContainerId() const;
    std::string generateContainerName(const std::string& base_name) const;
    std::map<std::string, std::string> createEventMetadata(const std::string& container_id,
                                                           const std::string& action) const;
};

// Container runtime factory
class ContainerRuntimeFactory {
public:
    static std::unique_ptr<ContainerRuntime> createRuntime();
    static std::unique_ptr<ContainerRuntime>
    createRuntime(const ContainerRuntime::RuntimeConfig& config);

    // Runtime validation
    static bool validateRuntimeEnvironment();
    static std::vector<std::string> getSystemRequirements();
    static std::vector<std::string> validateSystemConfiguration();
};

// Utility functions
namespace runtime_utils {
// Container validation
bool isValidContainerConfig(const ContainerConfig& config);
std::vector<std::string> validateContainerEnvironment();

// Resource calculations
size_t calculateMemoryLimit(const std::string& limit_str);
double calculateCpuShares(const std::string& shares_str);
std::chrono::microseconds calculateTimeLimit(const std::string& time_str);

// Path and name utilities
std::string sanitizeContainerName(const std::string& name);
std::string getContainerRootDir(const std::string& id);
std::string getContainerLogPath(const std::string& id);
std::string getContainerStatePath(const std::string& id);

// Configuration helpers
ContainerConfig createDefaultConfig(const std::string& image);
ContainerConfig mergeConfigs(const ContainerConfig& base, const ContainerConfig& override);
void applySecurityProfile(ContainerConfig& config, const std::string& profile_name);

// Performance utilities
std::chrono::milliseconds measureOperation(std::function<void()> operation);
ResourceStats calculateResourceDelta(const ResourceStats& before, const ResourceStats& after);
} // namespace runtime_utils

// Exception classes are defined in container_config.hpp to avoid duplicates

} // namespace runtime
} // namespace docker_cpp
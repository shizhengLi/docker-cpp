#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <signal.h>
#include "container_config.hpp"

namespace docker_cpp {
namespace core {
class Logger;
class EventManager;
} // namespace core

namespace plugin {
class PluginRegistry;
} // namespace plugin

namespace process {
class ProcessManager;
} // namespace process

namespace runtime {

// Forward declarations
class Container;

// Container event callback type
using ContainerEventCallback =
    std::function<void(const Container&, ContainerState, ContainerState)>;

// Container class representing a single container instance
class Container {
public:
    // Constructor
    explicit Container(const ContainerConfig& config);

    // Destructor
    ~Container();

    // Non-copyable, movable
    Container(const Container&) = delete;
    Container& operator=(const Container&) = delete;
    Container(Container&& other) noexcept;
    Container& operator=(Container&& other) noexcept;

    // Container lifecycle operations
    void start();
    void stop(int timeout = 10);
    void pause();
    void resume();
    void restart(int timeout = 10);
    void remove(bool force = false);
    void kill(int signal = SIGTERM);

    // State accessors
    ContainerState getState() const;
    ContainerInfo getInfo() const;
    std::chrono::system_clock::time_point getStartTime() const;
    std::chrono::system_clock::time_point getFinishedTime() const;
    int getExitCode() const;
    std::string getExitReason() const;

    // Process management
    pid_t getMainProcessPID() const;
    std::vector<pid_t> getAllProcesses() const;
    bool isProcessRunning() const;

    // Resource management
    void updateResources(const ResourceLimits& limits);
    ResourceStats getStats() const;
    void resetStats();

    // Event handling
    void setEventCallback(ContainerEventCallback callback);
    void removeEventCallback();

    // Configuration access
    const ContainerConfig& getConfig() const;
    void updateConfig(const ContainerConfig& config);

    // Monitoring
    void startMonitoring();
    void stopMonitoring();
    bool isMonitoring() const;

    // Health checks
    bool isHealthy() const;
    std::string getHealthStatus() const;

    // Cleanup
    void cleanup();

private:
    // Core data
    std::string id_;
    mutable std::mutex mutex_;
    ContainerConfig config_;
    std::atomic<ContainerState> state_;
    std::atomic<bool> removed_;

    // Process management
    // std::unique_ptr<process::ProcessManager> process_manager_;  // TODO: Replace with actual type
    std::atomic<pid_t> main_pid_;
    std::atomic<int> exit_code_;
    std::string exit_reason_;

    // Resource management (placeholders for future integration)
    // std::unique_ptr<void> cgroup_manager_;  // TODO: Replace with actual type
    // std::unique_ptr<void> namespace_managers_;  // TODO: Replace with actual type

    // Timing
    std::chrono::system_clock::time_point created_at_;
    std::chrono::system_clock::time_point started_at_;
    std::chrono::system_clock::time_point finished_at_;

    // Monitoring
    std::unique_ptr<std::thread> monitoring_thread_;
    std::atomic<bool> monitoring_active_;
    mutable std::mutex monitoring_mutex_;
    std::condition_variable monitoring_cv_;

    // Event handling
    ContainerEventCallback event_callback_;
    mutable std::mutex callback_mutex_;

    // Health checking
    std::unique_ptr<std::thread> healthcheck_thread_;
    std::atomic<bool> healthcheck_active_;
    std::atomic<bool> healthy_;
    std::string health_status_;
    std::chrono::system_clock::time_point last_healthcheck_;

    // Dependencies (injected)
    core::Logger* logger_;
    core::EventManager* event_manager_;
    plugin::PluginRegistry* plugin_registry_;

    // Private methods
    void transitionState(ContainerState new_state);
    void setupNamespaces();
    void setupCgroups();
    void startProcess();
    void monitorProcess();
    void startMonitoringThread();
    void stopMonitoringThread();
    void startHealthcheckThread();
    void stopHealthcheckThread();
    void executeHealthcheck();
    void waitForProcessExit(int timeout);
    void killProcess(int signal);
    void cleanupResources();
    void emitEvent(const std::string& event_type,
                   const std::map<std::string, std::string>& event_data);

    // State transition validation
    bool canTransitionTo(ContainerState new_state) const;
    std::vector<ContainerState> getValidTransitions(ContainerState from_state) const;
    void executeStateTransition(ContainerState new_state);
    void onStateEntered(ContainerState new_state);
    void onStateExited(ContainerState old_state);
    bool isStateTransitionValid(ContainerState from, ContainerState to) const;

    // Error handling
    void handleError(const std::string& error_msg);
    void setExitReason(const std::string& reason);

    // State machine configuration
    struct StateTransition {
        ContainerState from;
        ContainerState to;
        bool allowed;
        std::function<bool()> condition;
    };

    std::vector<StateTransition> getStateTransitionTable() const;
    void initializeStateMachine();

    // State-specific handlers
    void handleCreatedState();
    void handleStartingState();
    void handleRunningState();
    void handlePausedState();
    void handleStoppingState();
    void handleStoppedState();
    void handleRemovingState();
    void handleRemovedState();
    void handleDeadState();
    void handleRestartingState();
    void handleErrorState();

    // Utility methods
    std::string generateCgroupName() const;
    std::string generateLogPrefix() const;
    void logInfo(const std::string& message) const;
    void logError(const std::string& message) const;
    void logWarning(const std::string& message) const;
    void logDebug(const std::string& message) const;

    // State machine debugging
    void logStateTransition(ContainerState from, ContainerState to) const;
    std::string getStateDescription(ContainerState state) const;
};

// Container registry for managing multiple containers
class ContainerRegistry {
public:
    // Constructor
    ContainerRegistry(core::Logger* logger,
                      core::EventManager* event_manager,
                      plugin::PluginRegistry* plugin_registry);

    // Destructor
    ~ContainerRegistry();

    // Non-copyable, movable
    ContainerRegistry(const ContainerRegistry&) = delete;
    ContainerRegistry& operator=(const ContainerRegistry&) = delete;
    ContainerRegistry(ContainerRegistry&& other) noexcept;
    ContainerRegistry& operator=(ContainerRegistry&& other) noexcept;

    // Container management
    std::shared_ptr<Container> createContainer(const ContainerConfig& config);
    std::shared_ptr<Container> getContainer(const std::string& id) const;
    std::shared_ptr<Container> getContainerByName(const std::string& name) const;
    void removeContainer(const std::string& id, bool force = false);
    std::vector<std::shared_ptr<Container>> listContainers(bool all = false) const;
    std::vector<std::string> listContainerIds(bool all = false) const;

    // Container lifecycle operations
    void startContainer(const std::string& id);
    void stopContainer(const std::string& id, int timeout = 10);
    void pauseContainer(const std::string& id);
    void resumeContainer(const std::string& id);
    void restartContainer(const std::string& id, int timeout = 10);
    void killContainer(const std::string& id, int signal = SIGTERM);

    // Statistics and monitoring
    size_t getContainerCount() const;
    size_t getRunningContainerCount() const;
    std::vector<ResourceStats> getAllContainerStats() const;
    ResourceStats getAggregatedStats() const;

    // Cleanup
    void cleanupStoppedContainers();
    void cleanupRemovedContainers();
    void shutdown();

    // Event handling
    using ContainerEventCallback =
        std::function<void(const std::string&, const Container&, ContainerState, ContainerState)>;
    void setGlobalEventCallback(ContainerEventCallback callback);
    void removeGlobalEventCallback();

private:
    mutable std::mutex mutex_;
    std::map<std::string, std::shared_ptr<Container>> containers_;
    std::map<std::string, std::string> name_to_id_; // name -> id mapping

    // Dependencies
    core::Logger* logger_;
    core::EventManager* event_manager_;
    plugin::PluginRegistry* plugin_registry_;

    // Global event callback
    ContainerEventCallback global_callback_;
    mutable std::mutex callback_mutex_;

    // Private methods
    std::string generateUniqueId() const;
    std::string generateUniqueName(const std::string& base_name) const;
    bool isNameUnique(const std::string& name) const;
    void registerContainer(std::shared_ptr<Container> container);
    void unregisterContainer(const std::string& id);
    void onContainerEvent(const std::string& container_id,
                          const Container& container,
                          ContainerState old_state,
                          ContainerState new_state);
    void validateContainerConfig(const ContainerConfig& config) const;
    void logInfo(const std::string& message) const;
    void logError(const std::string& message) const;
};

// Utility functions
bool isValidContainerId(const std::string& id);
bool isValidContainerName(const std::string& name);
std::string generateContainerId();
std::string generateContainerName(const std::string& prefix = "docker-cpp-");

} // namespace runtime
} // namespace docker_cpp
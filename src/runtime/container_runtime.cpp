#include "container_runtime.hpp"
#include "core/logger.hpp"
#include "core/config_manager.hpp"
#include "core/event_manager.hpp"
#include "plugin/plugin_registry.hpp"
#include "namespace/namespace_manager.hpp"
#include "cgroup/cgroup_manager.hpp"
#include "process/process_manager.hpp"

#include <algorithm>
#include <sstream>
#include <chrono>
#include <thread>
#include <future>
#include <filesystem>

namespace dockercpp {
namespace runtime {

// ContainerRuntime implementation
ContainerRuntime::ContainerRuntime()
    : logger_(nullptr)
    , config_manager_(nullptr)
    , event_manager_(nullptr)
    , plugin_registry_(nullptr)
    , initialized_(false)
    , shutting_down_(false)
    , maintenance_active_(false)
    , maintenance_interval_(std::chrono::minutes(5)) {
    initialize();
}

ContainerRuntime::~ContainerRuntime() {
    shutdown();
}

ContainerRuntime::ContainerRuntime(ContainerRuntime&& other) noexcept
    : container_registry_(std::move(other.container_registry_))
    , config_manager_(std::move(other.config_manager_))
    , logger_(other.logger_)
    , event_manager_(other.event_manager_)
    , plugin_registry_(std::move(other.plugin_registry_))
    , runtime_config_(std::move(other.runtime_config_))
    , event_callbacks_(std::move(other.event_callbacks_))
    , initialized_(other.initialized_.load())
    , shutting_down_(other.shutting_down_.load())
    , maintenance_active_(other.maintenance_active_.load())
    , maintenance_interval_(other.maintenance_interval_) {
    other.logger_ = nullptr;
    other.event_manager_ = nullptr;
    other.initialized_ = false;
    other.shutting_down_ = true;
}

ContainerRuntime& ContainerRuntime::operator=(ContainerRuntime&& other) noexcept {
    if (this != &other) {
        shutdown();

        container_registry_ = std::move(other.container_registry_);
        config_manager_ = std::move(other.config_manager_);
        logger_ = other.logger_;
        event_manager_ = other.event_manager_;
        plugin_registry_ = std::move(other.plugin_registry_);
        runtime_config_ = std::move(other.runtime_config_);
        event_callbacks_ = std::move(other.event_callbacks_);
        initialized_ = other.initialized_.load();
        shutting_down_ = other.shutting_down_.load();
        maintenance_active_ = other.maintenance_active_.load();
        maintenance_interval_ = other.maintenance_interval_;

        other.logger_ = nullptr;
        other.event_manager_ = nullptr;
        other.initialized_ = false;
        other.shutting_down_ = true;
    }
    return *this;
}

void ContainerRuntime::initialize() {
    try {
        logInfo("Initializing Container Runtime");

        initializeComponents();
        loadPlugins();
        startMaintenanceThread();

        initialized_ = true;
        emitRuntimeEvent("runtime.init", {
            {"version", "1.0.0"},
            {"timestamp", std::to_string(std::chrono::system_clock::now().time_since_epoch().count())}
        });

        logInfo("Container Runtime initialized successfully");
    } catch (const std::exception& e) {
        logError("Failed to initialize Container Runtime: " + std::string(e.what()));
        throw;
    }
}

void ContainerRuntime::initializeComponents() {
    // Get logger instance
    logger_ = core::Logger::getInstance();

    // Create config manager
    config_manager_ = std::make_unique<core::ConfigManager>();

    // Get event manager instance
    event_manager_ = core::EventManager::getInstance();

    // Create plugin registry
    plugin_registry_ = std::make_unique<plugin::PluginRegistry>();

    // Create container registry
    container_registry_ = std::make_unique<ContainerRegistry>(logger_, event_manager_, plugin_registry_.get());

    logInfo("Core components initialized");
}

void ContainerRuntime::loadPlugins() {
    if (!plugin_registry_) {
        return;
    }

    // TODO: Load plugins from configured directory
    // For now, just log that we're ready to load plugins
    logInfo("Plugin system ready for loading plugins");
}

void ContainerRuntime::startMaintenanceThread() {
    maintenance_active_ = true;
    maintenance_thread_ = std::make_unique<std::thread>([this]() {
        while (maintenance_active_) {
            try {
                performMaintenanceTasks();
            } catch (const std::exception& e) {
                logError("Maintenance task failed: " + std::string(e.what()));
            }

            // Wait for next maintenance cycle
            std::unique_lock<std::mutex> lock(monitoring_mutex_);
            monitoring_cv_.wait_for(lock, maintenance_interval_, [this]() {
                return !maintenance_active_;
            });
        }
    });

    logInfo("Maintenance thread started");
}

void ContainerRuntime::stopMaintenanceThread() {
    maintenance_active_ = false;
    monitoring_cv_.notify_all();

    if (maintenance_thread_ && maintenance_thread_->joinable()) {
        maintenance_thread_->join();
    }

    logInfo("Maintenance thread stopped");
}

void ContainerRuntime::performMaintenanceTasks() {
    if (!container_registry_) {
        return;
    }

    // Cleanup stopped containers
    container_registry_->cleanupStoppedContainers();

    // Cleanup removed containers
    container_registry_->cleanupRemovedContainers();

    // TODO: Add other maintenance tasks like log rotation, cache cleanup, etc.
}

std::string ContainerRuntime::createContainer(const ContainerConfig& config) {
    if (shutting_down_) {
        throw ContainerRuntimeError("Runtime is shutting down");
    }

    validateContainerConfig(config);

    // Generate unique container ID and name
    std::string container_id = generateContainerId();
    std::string container_name = config.name.empty() ?
        generateContainerName("docker-cpp-") : config.name;

    // Create modified config with generated ID and name
    ContainerConfig final_config = config;
    final_config.id = container_id;
    final_config.name = container_name;

    try {
        auto container = container_registry_->createContainer(final_config);

        emitRuntimeEvent("container.create", createEventMetadata(container_id, "create"));
        logInfo("Container created: " + container_id + " (" + container_name + ")");

        return container_id;
    } catch (const std::exception& e) {
        logError("Failed to create container: " + std::string(e.what()));
        throw ContainerConfigurationError("Failed to create container: " + std::string(e.what()));
    }
}

void ContainerRuntime::startContainer(const std::string& container_id) {
    validateContainerId(container_id);
    validateContainerOperation(container_id, "start");

    try {
        container_registry_->startContainer(container_id);

        emitRuntimeEvent("container.start", createEventMetadata(container_id, "start"));
        logInfo("Container started: " + container_id);
    } catch (const std::exception& e) {
        logError("Failed to start container " + container_id + ": " + std::string(e.what()));
        throw;
    }
}

void ContainerRuntime::stopContainer(const std::string& container_id, int timeout) {
    validateContainerId(container_id);
    validateContainerOperation(container_id, "stop");

    try {
        container_registry_->stopContainer(container_id, timeout);

        emitRuntimeEvent("container.stop", createEventMetadata(container_id, "stop"));
        logInfo("Container stopped: " + container_id);
    } catch (const std::exception& e) {
        logError("Failed to stop container " + container_id + ": " + std::string(e.what()));
        throw;
    }
}

void ContainerRuntime::pauseContainer(const std::string& container_id) {
    validateContainerId(container_id);
    validateContainerOperation(container_id, "pause");

    try {
        container_registry_->pauseContainer(container_id);

        emitRuntimeEvent("container.pause", createEventMetadata(container_id, "pause"));
        logInfo("Container paused: " + container_id);
    } catch (const std::exception& e) {
        logError("Failed to pause container " + container_id + ": " + std::string(e.what()));
        throw;
    }
}

void ContainerRuntime::resumeContainer(const std::string& container_id) {
    validateContainerId(container_id);
    validateContainerOperation(container_id, "resume");

    try {
        container_registry_->resumeContainer(container_id);

        emitRuntimeEvent("container.resume", createEventMetadata(container_id, "resume"));
        logInfo("Container resumed: " + container_id);
    } catch (const std::exception& e) {
        logError("Failed to resume container " + container_id + ": " + std::string(e.what()));
        throw;
    }
}

void ContainerRuntime::restartContainer(const std::string& container_id, int timeout) {
    validateContainerId(container_id);
    validateContainerOperation(container_id, "restart");

    try {
        container_registry_->restartContainer(container_id, timeout);

        emitRuntimeEvent("container.restart", createEventMetadata(container_id, "restart"));
        logInfo("Container restarted: " + container_id);
    } catch (const std::exception& e) {
        logError("Failed to restart container " + container_id + ": " + std::string(e.what()));
        throw;
    }
}

void ContainerRuntime::removeContainer(const std::string& container_id, bool force) {
    validateContainerId(container_id);

    try {
        container_registry_->removeContainer(container_id, force);

        emitRuntimeEvent("container.remove", createEventMetadata(container_id, "remove"));
        logInfo("Container removed: " + container_id);
    } catch (const std::exception& e) {
        logError("Failed to remove container " + container_id + ": " + std::string(e.what()));
        throw;
    }
}

void ContainerRuntime::killContainer(const std::string& container_id, int signal) {
    validateContainerId(container_id);
    validateContainerOperation(container_id, "kill");

    try {
        container_registry_->killContainer(container_id, signal);

        emitRuntimeEvent("container.kill", createEventMetadata(container_id, "kill"));
        logInfo("Container killed: " + container_id + " with signal " + std::to_string(signal));
    } catch (const std::exception& e) {
        logError("Failed to kill container " + container_id + ": " + std::string(e.what()));
        throw;
    }
}

ContainerInfo ContainerRuntime::inspectContainer(const std::string& container_id) const {
    validateContainerId(container_id);

    auto container = container_registry_->getContainer(container_id);
    if (!container) {
        throw ContainerNotFoundError(container_id);
    }

    return container->getInfo();
}

std::vector<ContainerInfo> ContainerRuntime::listContainers(bool all) const {
    if (!container_registry_) {
        return {};
    }

    return container_registry_->listContainers(all);
}

std::vector<std::string> ContainerRuntime::listContainerIds(bool all) const {
    if (!container_registry_) {
        return {};
    }

    return container_registry_->listContainerIds(all);
}

size_t ContainerRuntime::getContainerCount() const {
    return container_registry_ ? container_registry_->getContainerCount() : 0;
}

size_t ContainerRuntime::getRunningContainerCount() const {
    return container_registry_ ? container_registry_->getRunningContainerCount() : 0;
}

ContainerState ContainerRuntime::getContainerState(const std::string& container_id) const {
    validateContainerId(container_id);

    auto container = container_registry_->getContainer(container_id);
    if (!container) {
        throw ContainerNotFoundError(container_id);
    }

    return container->getState();
}

void ContainerRuntime::waitForContainer(const std::string& container_id,
                                       ContainerState desired_state,
                                       int timeout_seconds) {
    validateContainerId(container_id);

    auto start_time = std::chrono::steady_clock::now();
    auto timeout_duration = std::chrono::seconds(timeout_seconds);

    while (std::chrono::steady_clock::now() - start_time < timeout_duration) {
        ContainerState current_state = getContainerState(container_id);
        if (current_state == desired_state) {
            return;
        }

        // Check if container is in a terminal state that won't reach desired state
        if (current_state == ContainerState::ERROR || current_state == ContainerState::DEAD) {
            throw ContainerRuntimeError("Container " + container_id + " is in error state");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    throw ContainerRuntimeError("Timeout waiting for container " + container_id +
                               " to reach state " + containerStateToString(desired_state));
}

std::future<void> ContainerRuntime::waitForContainerAsync(const std::string& container_id,
                                                        ContainerState desired_state,
                                                        int timeout_seconds) {
    return std::async(std::launch::async, [this, container_id, desired_state, timeout_seconds]() {
        waitForContainer(container_id, desired_state, timeout_seconds);
    });
}

void ContainerRuntime::updateContainerResources(const std::string& container_id,
                                              const ResourceLimits& limits) {
    validateContainerId(container_id);
    validateContainerOperation(container_id, "update_resources");

    try {
        auto container = container_registry_->getContainer(container_id);
        if (!container) {
            throw ContainerNotFoundError(container_id);
        }

        container->updateResources(limits);

        emitRuntimeEvent("container.update", createEventMetadata(container_id, "update_resources"));
        logInfo("Container resources updated: " + container_id);
    } catch (const std::exception& e) {
        logError("Failed to update resources for container " + container_id + ": " + std::string(e.what()));
        throw ResourceLimitError("Failed to update resources: " + std::string(e.what()));
    }
}

ResourceStats ContainerRuntime::getContainerStats(const std::string& container_id) const {
    validateContainerId(container_id);

    auto container = container_registry_->getContainer(container_id);
    if (!container) {
        throw ContainerNotFoundError(container_id);
    }

    return container->getStats();
}

std::vector<ResourceStats> ContainerRuntime::getAllContainerStats() const {
    return container_registry_ ? container_registry_->getAllContainerStats() : std::vector<ResourceStats>{};
}

ResourceStats ContainerRuntime::getAggregatedStats() const {
    return container_registry_ ? container_registry_->getAggregatedStats() : ResourceStats{};
}

ContainerRuntime::SystemInfo ContainerRuntime::getSystemInfo() const {
    SystemInfo info;

    if (container_registry_) {
        info.total_containers = container_registry_->getContainerCount();
        info.running_containers = container_registry_->getRunningContainerCount();

        // Calculate other container counts
        auto containers = container_registry_->listContainers(true);
        for (const auto& container : containers) {
            switch (container.state) {
                case ContainerState::PAUSED:
                    info.paused_containers++;
                    break;
                case ContainerState::STOPPED:
                case ContainerState::EXITED:
                case ContainerState::DEAD:
                    info.stopped_containers++;
                    break;
                default:
                    break;
            }
        }
    }

    // Get aggregated stats
    info.system_stats = getAggregatedStats();

    // System information
    info.version = "1.0.0";
    info.kernel_version = "Linux"; // TODO: Get actual kernel version
    info.operating_system = "Linux"; // TODO: Get actual OS
    info.system_time = std::chrono::system_clock::now();

    return info;
}

void ContainerRuntime::setRuntimeConfig(const RuntimeConfig& config) {
    validateRuntimeConfig(config);

    std::lock_guard<std::mutex> lock(config_mutex_);
    runtime_config_ = config;

    emitRuntimeEvent("runtime.config_updated", {
        {"config_version", "1.0"},
        {"timestamp", std::to_string(std::chrono::system_clock::now().time_since_epoch().count())}
    });

    logInfo("Runtime configuration updated");
}

ContainerRuntime::RuntimeConfig ContainerRuntime::getRuntimeConfig() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    return runtime_config_;
}

void ContainerRuntime::subscribeToEvents(RuntimeEventCallback callback,
                                        const std::vector<std::string>& event_types) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    event_callbacks_.emplace_back(event_types, callback);

    logInfo("Event subscription added");
}

void ContainerRuntime::unsubscribeFromEvents() {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    event_callbacks_.clear();

    logInfo("All event subscriptions removed");
}

void ContainerRuntime::pauseAllContainers() {
    if (!container_registry_) {
        return;
    }

    auto containers = container_registry_->listContainers(true);
    for (const auto& container : containers) {
        if (container.state == ContainerState::RUNNING) {
            try {
                pauseContainer(container.id);
            } catch (const std::exception& e) {
                logWarning("Failed to pause container " + container.id + ": " + std::string(e.what()));
            }
        }
    }

    logInfo("All containers paused");
}

void ContainerRuntime::resumeAllContainers() {
    if (!container_registry_) {
        return;
    }

    auto containers = container_registry_->listContainers(true);
    for (const auto& container : containers) {
        if (container.state == ContainerState::PAUSED) {
            try {
                resumeContainer(container.id);
            } catch (const std::exception& e) {
                logWarning("Failed to resume container " + container.id + ": " + std::string(e.what()));
            }
        }
    }

    logInfo("All containers resumed");
}

void ContainerRuntime::stopAllContainers(int timeout) {
    if (!container_registry_) {
        return;
    }

    auto containers = container_registry_->listContainers(true);
    for (const auto& container : containers) {
        if (container.state == ContainerState::RUNNING || container.state == ContainerState::PAUSED) {
            try {
                stopContainer(container.id, timeout);
            } catch (const std::exception& e) {
                logWarning("Failed to stop container " + container.id + ": " + std::string(e.what()));
            }
        }
    }

    logInfo("All containers stopped");
}

void ContainerRuntime::removeStoppedContainers() {
    if (container_registry_) {
        container_registry_->cleanupStoppedContainers();
        logInfo("Stopped containers removed");
    }
}

void ContainerRuntime::cleanupResources() {
    if (container_registry_) {
        container_registry_->cleanupStoppedContainers();
        container_registry_->cleanupRemovedContainers();
    }

    logInfo("Resource cleanup completed");
}

bool ContainerRuntime::isHealthy() const {
    if (!initialized_ || shutting_down_) {
        return false;
    }

    if (!container_registry_ || !logger_ || !event_manager_ || !plugin_registry_) {
        return false;
    }

    // TODO: Add more comprehensive health checks
    return true;
}

std::vector<std::string> ContainerRuntime::getHealthChecks() const {
    std::vector<std::string> checks;

    if (initialized_) {
        checks.push_back("✅ Runtime initialized");
    } else {
        checks.push_back("❌ Runtime not initialized");
    }

    if (shutting_down_) {
        checks.push_back("⚠️ Runtime shutting down");
    }

    if (container_registry_) {
        checks.push_back("✅ Container registry active");
    } else {
        checks.push_back("❌ Container registry not available");
    }

    if (logger_) {
        checks.push_back("✅ Logger active");
    } else {
        checks.push_back("❌ Logger not available");
    }

    if (event_manager_) {
        checks.push_back("✅ Event manager active");
    } else {
        checks.push_back("❌ Event manager not available");
    }

    if (plugin_registry_) {
        checks.push_back("✅ Plugin registry active");
    } else {
        checks.push_back("❌ Plugin registry not available");
    }

    return checks;
}

void ContainerRuntime::performMaintenance() {
    performMaintenanceTasks();
    logInfo("Manual maintenance completed");
}

void ContainerRuntime::shutdown() {
    if (shutting_down_) {
        return;
    }

    shutting_down_ = true;
    logInfo("Shutting down Container Runtime");

    try {
        // Stop all containers
        stopAllContainers(5);

        // Stop maintenance thread
        stopMaintenanceThread();

        // Cleanup resources
        cleanupResources();

        // Shutdown plugin registry
        if (plugin_registry_) {
            plugin_registry_.reset();
        }

        emitRuntimeEvent("runtime.shutdown", {
            {"timestamp", std::to_string(std::chrono::system_clock::now().time_since_epoch().count())}
        });

        initialized_ = false;
        logInfo("Container Runtime shutdown completed");
    } catch (const std::exception& e) {
        logError("Error during shutdown: " + std::string(e.what()));
    }
}

// Private methods
void ContainerRuntime::validateContainerConfig(const ContainerConfig& config) const {
    auto errors = config.validate();
    if (!errors.empty()) {
        std::string error_msg = "Invalid container configuration: ";
        for (const auto& error : errors) {
            error_msg += error + "; ";
        }
        throw ContainerConfigurationError(error_msg);
    }
}

void ContainerRuntime::validateContainerId(const std::string& id) const {
    if (!isValidContainerId(id)) {
        throw ContainerRuntimeError("Invalid container ID: " + id);
    }
}

void ContainerRuntime::validateContainerOperation(const std::string& id, const std::string& operation) const {
    auto container = container_registry_->getContainer(id);
    if (!container) {
        throw ContainerNotFoundError(id);
    }

    // TODO: Add more operation-specific validation based on container state
}

void ContainerRuntime::emitRuntimeEvent(const std::string& event_type,
                                      const std::map<std::string, std::string>& data) const {
    if (!event_manager_) {
        return;
    }

    try {
        Event event(event_type, data);
        event_manager_->publish(event);

        // Also notify local subscribers
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        for (const auto& [event_types, callback] : event_callbacks_) {
            if (event_types.empty() || std::find(event_types.begin(), event_types.end(), event_type) != event_types.end()) {
                try {
                    callback(event_type, data);
                } catch (const std::exception& e) {
                    logWarning("Event callback failed: " + std::string(e.what()));
                }
            }
        }
    } catch (const std::exception& e) {
        logWarning("Failed to emit event " + event_type + ": " + std::string(e.what()));
    }
}

std::shared_ptr<Container> ContainerRuntime::getContainer(const std::string& id) const {
    return container_registry_ ? container_registry_->getContainer(id) : nullptr;
}

std::string ContainerRuntime::generateContainerId() const {
    return runtime_utils::generateContainerId();
}

std::string ContainerRuntime::generateContainerName(const std::string& base_name) const {
    return runtime_utils::generateContainerName(base_name);
}

std::map<std::string, std::string> ContainerRuntime::createEventMetadata(const std::string& container_id,
                                                                        const std::string& action) const {
    return {
        {"container_id", container_id},
        {"action", action},
        {"timestamp", std::to_string(std::chrono::system_clock::now().time_since_epoch().count())}
    };
}

void ContainerRuntime::validateRuntimeConfig(const RuntimeConfig& config) const {
    // Basic validation for runtime configuration
    if (config.default_runtime.empty()) {
        throw ContainerRuntimeError("Default runtime cannot be empty");
    }

    if (config.cgroup_driver.empty()) {
        throw ContainerRuntimeError("Cgroup driver cannot be empty");
    }

    if (config.storage_driver.empty()) {
        throw ContainerRuntimeError("Storage driver cannot be empty");
    }
}

void ContainerRuntime::logInfo(const std::string& message) const {
    if (logger_) {
        logger_->info("ContainerRuntime", message);
    }
}

void ContainerRuntime::logError(const std::string& message) const {
    if (logger_) {
        logger_->error("ContainerRuntime", message);
    }
}

void ContainerRuntime::logWarning(const std::string& message) const {
    if (logger_) {
        logger_->warning("ContainerRuntime", message);
    }
}

// ContainerRuntimeFactory implementation
std::unique_ptr<ContainerRuntime> ContainerRuntimeFactory::createRuntime() {
    return std::make_unique<ContainerRuntime>();
}

std::unique_ptr<ContainerRuntime> ContainerRuntimeFactory::createRuntime(const ContainerRuntime::RuntimeConfig& config) {
    auto runtime = std::make_unique<ContainerRuntime>();
    runtime->setRuntimeConfig(config);
    return runtime;
}

bool ContainerRuntimeFactory::validateRuntimeEnvironment() {
    // TODO: Implement actual environment validation
    // Check for required system capabilities, permissions, etc.
    return true;
}

std::vector<std::string> ContainerRuntimeFactory::getSystemRequirements() {
    return {
        "Linux kernel 4.15+",
        "Cgroup v2 support",
        "Namespace support",
        "Root privileges or user namespace support",
        "Sufficient memory and disk space"
    };
}

std::vector<std::string> ContainerRuntimeFactory::validateSystemConfiguration() {
    std::vector<std::string> issues;

    // TODO: Implement actual system configuration validation
    // Check for cgroup mount points, kernel parameters, etc.

    return issues;
}

// Runtime utility functions
namespace runtime_utils {

bool isValidContainerConfig(const ContainerConfig& config) {
    return config.isValid();
}

std::vector<std::string> validateContainerEnvironment() {
    std::vector<std::string> issues;

    // TODO: Implement container environment validation
    // Check for available resources, system limits, etc.

    return issues;
}

size_t calculateMemoryLimit(const std::string& limit_str) {
    if (limit_str.empty()) {
        return 0;
    }

    // Simple parsing for common memory formats
    if (limit_str.back() == 'b' || limit_str.back() == 'B') {
        return std::stoull(limit_str.substr(0, limit_str.length() - 1));
    } else if (limit_str.back() == 'k' || limit_str.back() == 'K') {
        return std::stoull(limit_str.substr(0, limit_str.length() - 1)) * 1024;
    } else if (limit_str.back() == 'm' || limit_str.back() == 'M') {
        return std::stoull(limit_str.substr(0, limit_str.length() - 1)) * 1024 * 1024;
    } else if (limit_str.back() == 'g' || limit_str.back() == 'G') {
        return std::stoull(limit_str.substr(0, limit_str.length() - 1)) * 1024 * 1024 * 1024;
    } else {
        return std::stoull(limit_str);
    }
}

double calculateCpuShares(const std::string& shares_str) {
    return std::stod(shares_str);
}

std::chrono::microseconds calculateTimeLimit(const std::string& time_str) {
    return std::chrono::microseconds(std::stoull(time_str));
}

std::string sanitizeContainerName(const std::string& name) {
    std::string sanitized = name;

    // Replace invalid characters with hyphens
    std::replace_if(sanitized.begin(), sanitized.end(), [](char c) {
        return !std::isalnum(c) && c != '_' && c != '-' && c != '.';
    }, '-');

    // Remove leading and trailing hyphens and dots
    sanitized.erase(0, sanitized.find_first_not_of("-."));
    sanitized.erase(sanitized.find_last_not_of("-.") + 1);

    // Ensure it starts with a letter
    if (!sanitized.empty() && std::isdigit(sanitized[0])) {
        sanitized = "c-" + sanitized;
    }

    // Limit length
    if (sanitized.length() > 63) {
        sanitized = sanitized.substr(0, 63);
    }

    return sanitized.empty() ? "container" : sanitized;
}

std::string getContainerRootDir(const std::string& id) {
    return "/var/lib/docker-cpp/containers/" + id;
}

std::string getContainerLogPath(const std::string& id) {
    return getContainerRootDir(id) + "/container.log";
}

std::string getContainerStatePath(const std::string& id) {
    return getContainerRootDir(id) + "/state.json";
}

ContainerConfig createDefaultConfig(const std::string& image) {
    ContainerConfig config;
    config.image = image;
    config.command = {"/bin/sh"};
    config.working_dir = "/";
    config.interactive = false;
    config.tty = false;
    config.attach_stdin = false;
    config.attach_stdout = true;
    config.attach_stderr = true;

    // Set default resource limits
    config.resources.cpu_shares = 1.0;
    config.resources.cpu_period = 100000;
    config.resources.memory_limit = 0; // Unlimited

    // Set default security settings
    config.security.no_new_privileges = true;
    config.security.read_only_rootfs = false;

    // Set default restart policy
    config.restart_policy.policy = RestartPolicy::NO;

    return config;
}

ContainerConfig mergeConfigs(const ContainerConfig& base, const ContainerConfig& override) {
    ContainerConfig merged = base;

    // Override basic fields
    if (!override.name.empty()) merged.name = override.name;
    if (!override.image.empty()) merged.image = override.image;
    if (!override.command.empty()) merged.command = override.command;
    if (!override.args.empty()) merged.args = override.args;
    if (!override.working_dir.empty()) merged.working_dir = override.working_dir;

    // Override interactive settings
    merged.interactive = override.interactive;
    merged.tty = override.tty;
    merged.attach_stdin = override.attach_stdin;
    merged.attach_stdout = override.attach_stdout;
    merged.attach_stderr = override.attach_stderr;

    // Merge environment variables
    for (const auto& env : override.env) {
        merged.setEnvironment(env.substr(0, env.find('=')), env.substr(env.find('=') + 1));
    }

    // Merge labels
    for (const auto& [key, value] : override.labels) {
        merged.labels[key] = value;
    }

    // Override resource limits if specified
    if (override.resources.memory_limit != 0) merged.resources.memory_limit = override.resources.memory_limit;
    if (override.resources.cpu_shares != 1.0) merged.resources.cpu_shares = override.resources.cpu_shares;

    // TODO: Merge other configuration sections as needed

    return merged;
}

void applySecurityProfile(ContainerConfig& config, const std::string& profile_name) {
    // TODO: Implement security profile application
    // This would load predefined security profiles and apply them to the config
}

std::chrono::milliseconds measureOperation(std::function<void()> operation) {
    auto start = std::chrono::high_resolution_clock::now();
    operation();
    auto end = std::chrono::high_resolution_clock::now();

    return std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
}

ResourceStats calculateResourceDelta(const ResourceStats& before, const ResourceStats& after) {
    ResourceStats delta;

    delta.memory_usage_bytes = after.memory_usage_bytes - before.memory_usage_bytes;
    delta.cpu_time_nanos = after.cpu_time_nanos - before.cpu_time_nanos;
    delta.network_rx_bytes = after.network_rx_bytes - before.network_rx_bytes;
    delta.network_tx_bytes = after.network_tx_bytes - before.network_tx_bytes;
    delta.blkio_read_bytes = after.blkio_read_bytes - before.blkio_read_bytes;
    delta.blkio_write_bytes = after.blkio_write_bytes - before.blkio_write_bytes;

    return delta;
}

} // namespace runtime_utils

} // namespace runtime
} // namespace dockercpp
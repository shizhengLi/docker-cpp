#include "container.hpp"
#include <algorithm>
#include <iostream>
#include <random>

namespace docker_cpp {
namespace runtime {

// ContainerRegistry implementation
ContainerRegistry::ContainerRegistry(core::Logger* logger,
                                    core::EventManager* event_manager,
                                    plugin::PluginRegistry* plugin_registry)
    : logger_(logger)
    , event_manager_(event_manager)
    , plugin_registry_(plugin_registry) {
    logInfo("ContainerRegistry initialized");
}

ContainerRegistry::~ContainerRegistry() {
    shutdown();
}

ContainerRegistry::ContainerRegistry(ContainerRegistry&& other) noexcept
    : containers_(std::move(other.containers_))
    , name_to_id_(std::move(other.name_to_id_))
    , logger_(other.logger_)
    , event_manager_(other.event_manager_)
    , plugin_registry_(std::move(other.plugin_registry_))
    , global_callback_(std::move(other.global_callback_)) {

    other.logger_ = nullptr;
    other.event_manager_ = nullptr;
}

ContainerRegistry& ContainerRegistry::operator=(ContainerRegistry&& other) noexcept {
    if (this != &other) {
        shutdown();

        containers_ = std::move(other.containers_);
        name_to_id_ = std::move(other.name_to_id_);
        logger_ = other.logger_;
        event_manager_ = other.event_manager_;
        plugin_registry_ = std::move(other.plugin_registry_);
        global_callback_ = std::move(other.global_callback_);

        other.logger_ = nullptr;
        other.event_manager_ = nullptr;
    }
    return *this;
}

std::shared_ptr<Container> ContainerRegistry::createContainer(const ContainerConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);

    validateContainerConfig(config);

    // Generate unique ID if not provided
    std::string container_id = config.id.empty() ? generateUniqueId() : config.id;

    // Ensure ID is unique
    if (containers_.find(container_id) != containers_.end()) {
        throw ContainerRuntimeError("Container ID already exists: " + container_id);
    }

    // Generate unique name if needed
    std::string container_name = config.name;
    if (container_name.empty()) {
        container_name = generateUniqueName("docker-cpp-");
    } else {
        // Ensure name is unique
        if (!isNameUnique(container_name)) {
            container_name = generateUniqueName(container_name + "-");
        }
    }

    // Create modified config with generated ID and name
    ContainerConfig final_config = config;
    final_config.id = container_id;
    final_config.name = container_name;

    try {
        // Create container
        auto container = std::make_shared<Container>(final_config);

        // Set up event callback
        container->setEventCallback([this](const Container& cont, ContainerState old_state, ContainerState new_state) {
            onContainerEvent(cont.getInfo().id, cont, old_state, new_state);
        });

        // Register container
        registerContainer(container);

        logInfo("Container created: " + container_id + " (" + container_name + ")");

        return container;

    } catch (const std::exception& e) {
        logError("Failed to create container: " + std::string(e.what()));
        throw ContainerConfigurationError("Failed to create container: " + std::string(e.what()));
    }
}

std::shared_ptr<Container> ContainerRegistry::getContainer(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = containers_.find(id);
    return it != containers_.end() ? it->second : nullptr;
}

std::shared_ptr<Container> ContainerRegistry::getContainerByName(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = name_to_id_.find(name);
    if (it != name_to_id_.end()) {
        return getContainer(it->second);
    }

    return nullptr;
}

void ContainerRegistry::removeContainer(const std::string& id, bool force) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto container = getContainer(id);
    if (!container) {
        throw ContainerNotFoundError(id);
    }

    try {
        container->remove(force);
        unregisterContainer(id);

        logInfo("Container removed: " + id);

    } catch (const std::exception& e) {
        logError("Failed to remove container " + id + ": " + std::string(e.what()));
        throw;
    }
}

std::vector<std::shared_ptr<Container>> ContainerRegistry::listContainers(bool all) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::shared_ptr<Container>> result;

    for (const auto& [id, container] : containers_) {
        if (all || container->getState() == ContainerState::RUNNING) {
            result.push_back(container);
        }
    }

    return result;
}

std::vector<std::string> ContainerRegistry::listContainerIds(bool all) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::string> result;

    for (const auto& [id, container] : containers_) {
        if (all || container->getState() == ContainerState::RUNNING) {
            result.push_back(id);
        }
    }

    return result;
}

void ContainerRegistry::startContainer(const std::string& id) {
    auto container = getContainer(id);
    if (!container) {
        throw ContainerNotFoundError(id);
    }

    container->start();
}

void ContainerRegistry::stopContainer(const std::string& id, int timeout) {
    auto container = getContainer(id);
    if (!container) {
        throw ContainerNotFoundError(id);
    }

    container->stop(timeout);
}

void ContainerRegistry::pauseContainer(const std::string& id) {
    auto container = getContainer(id);
    if (!container) {
        throw ContainerNotFoundError(id);
    }

    container->pause();
}

void ContainerRegistry::resumeContainer(const std::string& id) {
    auto container = getContainer(id);
    if (!container) {
        throw ContainerNotFoundError(id);
    }

    container->resume();
}

void ContainerRegistry::restartContainer(const std::string& id, int timeout) {
    auto container = getContainer(id);
    if (!container) {
        throw ContainerNotFoundError(id);
    }

    container->restart(timeout);
}

void ContainerRegistry::killContainer(const std::string& id, int signal) {
    auto container = getContainer(id);
    if (!container) {
        throw ContainerNotFoundError(id);
    }

    container->kill(signal);
}

size_t ContainerRegistry::getContainerCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return containers_.size();
}

size_t ContainerRegistry::getRunningContainerCount() const {
    std::lock_guard<std::mutex> lock(mutex_);

    size_t count = 0;
    for (const auto& [id, container] : containers_) {
        if (container->getState() == ContainerState::RUNNING) {
            count++;
        }
    }

    return count;
}

std::vector<ResourceStats> ContainerRegistry::getAllContainerStats() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<ResourceStats> stats;

    for (const auto& [id, container] : containers_) {
        if (container->getState() == ContainerState::RUNNING) {
            stats.push_back(container->getStats());
        }
    }

    return stats;
}

ResourceStats ContainerRegistry::getAggregatedStats() const {
    std::lock_guard<std::mutex> lock(mutex_);

    ResourceStats aggregated;

    for (const auto& [id, container] : containers_) {
        if (container->getState() == ContainerState::RUNNING) {
            auto stats = container->getStats();
            aggregated.memory_usage_bytes += stats.memory_usage_bytes;
            aggregated.cpu_usage_percent += stats.cpu_usage_percent;
            aggregated.current_pids += stats.current_pids;
            aggregated.network_rx_bytes += stats.network_rx_bytes;
            aggregated.network_tx_bytes += stats.network_tx_bytes;
            aggregated.blkio_read_bytes += stats.blkio_read_bytes;
            aggregated.blkio_write_bytes += stats.blkio_write_bytes;
        }
    }

    aggregated.timestamp = std::chrono::system_clock::now();

    return aggregated;
}

void ContainerRegistry::cleanupStoppedContainers() {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::string> to_remove;

    for (const auto& [id, container] : containers_) {
        ContainerState state = container->getState();
        if (state == ContainerState::STOPPED || state == ContainerState::DEAD ||
            state == ContainerState::ERROR) {
            // Check if container has been stopped for more than 5 minutes
            auto info = container->getInfo();
            auto time_since_stop = std::chrono::system_clock::now() - info.finished_at;
            if (time_since_stop > std::chrono::minutes(5)) {
                to_remove.push_back(id);
            }
        }
    }

    for (const auto& id : to_remove) {
        try {
            auto container = containers_[id];
            container->remove(true);
            unregisterContainer(id);
            logInfo("Auto-removed stopped container: " + id);
        } catch (const std::exception& e) {
            logError("Failed to auto-remove container " + id + ": " + std::string(e.what()));
        }
    }

    if (!to_remove.empty()) {
        logInfo("Cleaned up " + std::to_string(to_remove.size()) + " stopped containers");
    }
}

void ContainerRegistry::cleanupRemovedContainers() {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::string> to_remove;

    for (const auto& [id, container] : containers_) {
        if (container->getState() == ContainerState::REMOVED) {
            to_remove.push_back(id);
        }
    }

    for (const auto& id : to_remove) {
        containers_.erase(id);
    }

    if (!to_remove.empty()) {
        logInfo("Cleaned up " + std::to_string(to_remove.size()) + " removed containers from registry");
    }
}

void ContainerRegistry::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Stop all running containers
    for (const auto& [id, container] : containers_) {
        if (container->getState() == ContainerState::RUNNING || container->getState() == ContainerState::PAUSED) {
            try {
                container->stop(5);
                logInfo("Stopped container during shutdown: " + id);
            } catch (const std::exception& e) {
                logError("Failed to stop container " + id + " during shutdown: " + std::string(e.what()));
            }
        }
    }

    // Clear all containers
    containers_.clear();
    name_to_id_.clear();

    // Remove global callback
    {
        std::lock_guard<std::mutex> callback_lock(callback_mutex_);
        global_callback_ = nullptr;
    }

    logInfo("ContainerRegistry shutdown completed");
}

void ContainerRegistry::setGlobalEventCallback(ContainerEventCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    global_callback_ = callback;
}

void ContainerRegistry::removeGlobalEventCallback() {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    global_callback_ = nullptr;
}

// Private methods
std::string ContainerRegistry::generateUniqueId() const {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);

    std::stringstream ss;
    for (int i = 0; i < 64; ++i) {
        ss << std::hex << dis(gen);
    }

    return ss.str();
}

std::string ContainerRegistry::generateUniqueName(const std::string& base_name) const {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 35);

    std::string name = base_name;

    // Generate 6 random characters
    for (int i = 0; i < 6; ++i) {
        int num = dis(gen);
        if (num < 10) {
            name += std::to_string(num);
        } else {
            name += static_cast<char>('a' + (num - 10));
        }
    }

    // If still not unique, append a counter
    int counter = 1;
    std::string unique_name = name;
    while (!isNameUnique(unique_name)) {
        unique_name = name + "-" + std::to_string(counter++);
    }

    return unique_name;
}

bool ContainerRegistry::isNameUnique(const std::string& name) const {
    return name_to_id_.find(name) == name_to_id_.end();
}

void ContainerRegistry::registerContainer(std::shared_ptr<Container> container) {
    const std::string& id = container->getInfo().id;
    const std::string& name = container->getConfig().name;

    containers_[id] = container;
    name_to_id_[name] = id;

    logInfo("Container registered: " + id + " (" + name + ")");
}

void ContainerRegistry::unregisterContainer(const std::string& id) {
    auto it = containers_.find(id);
    if (it != containers_.end()) {
        const std::string& name = it->second->getConfig().name;
        containers_.erase(it);
        name_to_id_.erase(name);

        logInfo("Container unregistered: " + id + " (" + name + ")");
    }
}

void ContainerRegistry::onContainerEvent(const std::string& container_id,
                                       const Container& container,
                                       ContainerState old_state,
                                       ContainerState new_state) {
    // Call global callback if set
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (global_callback_) {
            try {
                global_callback_(container_id, container, old_state, new_state);
            } catch (const std::exception& e) {
                logError("Global event callback failed: " + std::string(e.what()));
            }
        }
    }

    // Log state transition
    logInfo("Container " + container_id + " transitioned from " +
            containerStateToString(old_state) + " to " + containerStateToString(new_state));
}

void ContainerRegistry::validateContainerConfig(const ContainerConfig& config) const {
    auto errors = config.validate();
    if (!errors.empty()) {
        std::string error_msg = "Invalid container configuration: ";
        for (const auto& error : errors) {
            error_msg += error + "; ";
        }
        throw ContainerConfigurationError(error_msg);
    }
}

void ContainerRegistry::logInfo(const std::string& message) const {
    // TODO: Implement logging when Logger integration is complete
    std::cout << "[ContainerRegistry] " << message << std::endl;
}

void ContainerRegistry::logError(const std::string& message) const {
    // TODO: Implement logging when Logger integration is complete
    std::cerr << "[ContainerRegistry] ERROR: " << message << std::endl;
}

// void ContainerRegistry::logWarning(const std::string& message) const {
//     // TODO: Implement logging when Logger integration is complete
//     std::cout << "[ContainerRegistry] WARNING: " << message << std::endl;
// }

} // namespace runtime
} // namespace docker_cpp
#include "container.hpp"
#include <docker-cpp/core/logger.hpp>
#include <docker-cpp/core/event.hpp>
#include <docker-cpp/plugin/plugin_registry.hpp>
#include <docker-cpp/namespace/namespace_manager.hpp>
#include <docker-cpp/cgroup/cgroup_manager.hpp>
#include <docker-cpp/namespace/process_manager.hpp>

#include <algorithm>
#include <iostream>
#include <thread>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstring>
#include <errno.h>

namespace docker_cpp {
namespace runtime {

// Container implementation
Container::Container(const ContainerConfig& config)
    : id_(config.id.empty() ? generateContainerId() : config.id)
    , config_(config)
    , state_(ContainerState::CREATED)
    , removed_(false)
    , main_pid_(0)
    , exit_code_(0)
    , created_at_(std::chrono::system_clock::now())
    , monitoring_active_(false)
    , healthcheck_active_(false)
    , healthy_(true)
    , health_status_("healthy")
    , logger_(nullptr)
    , event_manager_(nullptr)
    , plugin_registry_(nullptr) {

    // Update config with our generated ID
    config_.id = id_;

    // Set default timestamps
    started_at_ = created_at_;
    finished_at_ = created_at_;

    logInfo("Container created: " + id_);
    emitEvent("container.created", {
        {"container_id", id_},
        {"image", config_.image},
        {"name", config_.name}
    });
}

Container::~Container() {
    cleanup();
}

Container::Container(Container&& other) noexcept
    : id_(std::move(other.id_))
    , config_(std::move(other.config_))
    , state_(other.state_.load())
    , removed_(other.removed_.load())
    , main_pid_(other.main_pid_.load())
    , exit_code_(other.exit_code_.load())
    , exit_reason_(std::move(other.exit_reason_))
    , created_at_(other.created_at_)
    , started_at_(other.started_at_)
    , finished_at_(other.finished_at_)
    , monitoring_active_(other.monitoring_active_.load())
    , healthy_(other.healthy_.load())
    , health_status_(std::move(other.health_status_))
    , last_healthcheck_(other.last_healthcheck_)
    , logger_(other.logger_)
    , event_manager_(other.event_manager_)
    , plugin_registry_(other.plugin_registry_) {

    // Don't move mutex - it can't be moved
    other.monitoring_active_ = false;
    other.healthcheck_active_ = false;
}

Container& Container::operator=(Container&& other) noexcept {
    if (this != &other) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::lock_guard<std::mutex> other_lock(other.mutex_);

        id_ = std::move(other.id_);
        config_ = std::move(other.config_);
        state_ = other.state_.load();
        removed_ = other.removed_.load();
        main_pid_ = other.main_pid_.load();
        exit_code_ = other.exit_code_.load();
        exit_reason_ = std::move(other.exit_reason_);
        created_at_ = other.created_at_;
        started_at_ = other.started_at_;
        finished_at_ = other.finished_at_;
        monitoring_active_ = other.monitoring_active_.load();
        healthy_ = other.healthy_.load();
        health_status_ = std::move(other.health_status_);
        last_healthcheck_ = other.last_healthcheck_;
        logger_ = other.logger_;
        event_manager_ = other.event_manager_;
        plugin_registry_ = other.plugin_registry_;

        other.monitoring_active_ = false;
        other.healthcheck_active_ = false;
    }
    return *this;
}

void Container::start() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!canTransitionTo(ContainerState::STARTING)) {
        throw InvalidContainerStateError(id_, state_.load(), ContainerState::STARTING);
    }

    transitionState(ContainerState::STARTING);
    logInfo("Starting container: " + id_);

    try {
        startProcess();
        setupNamespaces();
        setupCgroups();

        transitionState(ContainerState::RUNNING);
        started_at_ = std::chrono::system_clock::now();

        startMonitoring();
        startHealthcheckThread();

        emitEvent("container.started", {
            {"container_id", id_},
            {"pid", std::to_string(main_pid_.load())}
        });

        logInfo("Container started successfully: " + id_);
    } catch (const std::exception& e) {
        transitionState(ContainerState::ERROR);
        logError("Failed to start container: " + std::string(e.what()));
        throw;
    }
}

void Container::stop(int timeout) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!canTransitionTo(ContainerState::STOPPING)) {
        throw InvalidContainerStateError(id_, state_.load(), ContainerState::STOPPING);
    }

    transitionState(ContainerState::STOPPING);
    logInfo("Stopping container: " + id_);

    try {
        if (main_pid_.load() > 0) {
            // Send SIGTERM first
            ::kill(main_pid_.load(), SIGTERM);

            // Wait for graceful shutdown
            waitForProcessExit(timeout);

            // If still running, force kill
            if (isProcessRunning()) {
                ::kill(main_pid_.load(), SIGKILL);
                waitForProcessExit(5);
            }
        }

        transitionState(ContainerState::STOPPED);
        finished_at_ = std::chrono::system_clock::now();

        stopMonitoring();
        stopHealthcheckThread();

        emitEvent("container.stopped", {
            {"container_id", id_},
            {"exit_code", std::to_string(exit_code_.load())}
        });

        logInfo("Container stopped successfully: " + id_);
    } catch (const std::exception& e) {
        transitionState(ContainerState::ERROR);
        logError("Failed to stop container: " + std::string(e.what()));
        throw;
    }
}

void Container::pause() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!canTransitionTo(ContainerState::PAUSED)) {
        throw InvalidContainerStateError(id_, state_.load(), ContainerState::PAUSED);
    }

    transitionState(ContainerState::PAUSED);
    logInfo("Container paused: " + id_);

    emitEvent("container.paused", {{"container_id", id_}});
}

void Container::resume() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!canTransitionTo(ContainerState::RUNNING)) {
        throw InvalidContainerStateError(id_, state_.load(), ContainerState::RUNNING);
    }

    transitionState(ContainerState::RUNNING);
    logInfo("Container resumed: " + id_);

    emitEvent("container.resumed", {{"container_id", id_}});
}

void Container::restart(int timeout) {
    logInfo("Restarting container: " + id_);
    stop(timeout);
    start();
}

void Container::remove(bool force) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (state_.load() == ContainerState::RUNNING && !force) {
        throw InvalidContainerStateError(id_, state_.load(), ContainerState::REMOVED);
    }

    transitionState(ContainerState::REMOVING);
    logInfo("Removing container: " + id_);

    try {
        if (force && state_.load() == ContainerState::RUNNING) {
            ::kill(main_pid_.load(), SIGKILL);
            waitForProcessExit(5);
        }

        cleanupResources();
        transitionState(ContainerState::REMOVED);
        removed_ = true;

        emitEvent("container.removed", {{"container_id", id_}});
        logInfo("Container removed successfully: " + id_);
    } catch (const std::exception& e) {
        transitionState(ContainerState::ERROR);
        logError("Failed to remove container: " + std::string(e.what()));
        throw;
    }
}

void Container::kill(int signal) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (main_pid_.load() > 0) {
        logInfo("Killing container " + id_ + " with signal " + std::to_string(signal));
        ::kill(main_pid_.load(), signal);
    }
}

ContainerState Container::getState() const {
    return state_.load();
}

ContainerInfo Container::getInfo() const {
    std::lock_guard<std::mutex> lock(mutex_);

    ContainerInfo info;
    info.id = id_;
    info.name = config_.name;
    info.image = config_.image;
    info.state = state_.load();
    info.created_at = created_at_;
    info.started_at = started_at_;
    info.finished_at = finished_at_;
    info.exit_code = exit_code_.load();
    info.error = exit_reason_;

    return info;
}

std::chrono::system_clock::time_point Container::getStartTime() const {
    return started_at_;
}

std::chrono::system_clock::time_point Container::getFinishedTime() const {
    return finished_at_;
}

int Container::getExitCode() const {
    return exit_code_.load();
}

std::string Container::getExitReason() const {
    return exit_reason_;
}

pid_t Container::getMainProcessPID() const {
    return main_pid_.load();
}

std::vector<pid_t> Container::getAllProcesses() const {
    if (main_pid_.load() > 0) {
        return {main_pid_.load()};
    }
    return {};
}

bool Container::isProcessRunning() const {
    pid_t pid = main_pid_.load();
    if (pid <= 0) return false;

    return ::kill(pid, 0) == 0;
}

void Container::updateResources(const ResourceLimits& limits) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (state_.load() != ContainerState::RUNNING) {
        throw InvalidContainerStateError(id_, state_.load(), ContainerState::RUNNING);
    }

    // TODO: Implement resource updates when cgroup integration is complete
    logInfo("Resource limits updated for container: " + id_);
}

ResourceStats Container::getStats() const {
    // TODO: Implement actual resource statistics collection
    ResourceStats stats;
    stats.memory_usage_bytes = 0;
    stats.cpu_usage_percent = 0.0;
    stats.network_rx_bytes = 0;
    stats.network_tx_bytes = 0;
    stats.blkio_read_bytes = 0;
    stats.blkio_write_bytes = 0;
    stats.timestamp = std::chrono::system_clock::now();

    return stats;
}

void Container::resetStats() {
    // TODO: Implement statistics reset
    logInfo("Statistics reset for container: " + id_);
}

void Container::setEventCallback(ContainerEventCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    event_callback_ = callback;
}

void Container::removeEventCallback() {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    event_callback_ = nullptr;
}

const ContainerConfig& Container::getConfig() const {
    return config_;
}

void Container::updateConfig(const ContainerConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
}

void Container::startMonitoring() {
    if (monitoring_active_.load()) return;

    monitoring_active_ = true;
    startMonitoringThread();
}

void Container::stopMonitoring() {
    monitoring_active_ = false;
    stopMonitoringThread();
}

bool Container::isMonitoring() const {
    return monitoring_active_.load();
}

bool Container::isHealthy() const {
    return healthy_.load();
}

std::string Container::getHealthStatus() const {
    return health_status_;
}

void Container::cleanup() {
    if (removed_.load()) return;

    try {
        stopMonitoring();
        stopHealthcheckThread();
        cleanupResources();
        removed_ = true;
    } catch (const std::exception& e) {
        logError("Error during cleanup: " + std::string(e.what()));
    }
}

// Private methods
void Container::transitionState(ContainerState new_state) {
    ContainerState old_state = state_.exchange(new_state);

    logInfo("Container " + id_ + " transitioned from " +
            containerStateToString(old_state) + " to " +
            containerStateToString(new_state));

    // Notify callback if set
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (event_callback_) {
            event_callback_(*this, old_state, new_state);
        }
    }
}

void Container::setupNamespaces() {
    // TODO: Implement namespace setup using Phase 1 components
    logInfo("Setting up namespaces for container: " + id_);
}

void Container::setupCgroups() {
    // TODO: Implement cgroup setup using Phase 1 components
    logInfo("Setting up cgroups for container: " + id_);
}

void Container::startProcess() {
    // Create a simple process for now
    ProcessConfig proc_config;
    proc_config.executable = "/bin/sleep";
    proc_config.args = {"infinity"};
    proc_config.working_dir = config_.working_dir;
    proc_config.env = config_.env;

    // TODO: This will be implemented when ProcessManager integration is complete
    logInfo("Starting process for container: " + id_);
    main_pid_ = 0; // Placeholder
}

void Container::monitorProcess() {
    // TODO: Implement process monitoring
    logInfo("Monitoring process for container: " + id_);
}

void Container::startMonitoringThread() {
    // TODO: Implement monitoring thread
    logInfo("Starting monitoring thread for container: " + id_);
}

void Container::stopMonitoringThread() {
    // TODO: Implement monitoring thread cleanup
    logInfo("Stopping monitoring thread for container: " + id_);
}

void Container::startHealthcheckThread() {
    // TODO: Implement health check thread
    logInfo("Starting health check thread for container: " + id_);
}

void Container::stopHealthcheckThread() {
    // TODO: Implement health check thread cleanup
    logInfo("Stopping health check thread for container: " + id_);
}

void Container::executeHealthcheck() {
    // TODO: Implement health check execution
    healthy_ = true;
    health_status_ = "healthy";
    last_healthcheck_ = std::chrono::system_clock::now();
}

void Container::waitForProcessExit(int timeout) {
    if (main_pid_.load() <= 0) return;

    int status;
    pid_t result = waitpid(main_pid_.load(), &status, WNOHANG);

    if (result == 0) {
        // Process is still running, wait for timeout
        std::this_thread::sleep_for(std::chrono::seconds(timeout));
        result = waitpid(main_pid_.load(), &status, WNOHANG);

        if (result == 0) {
            logWarning("Process did not exit within timeout for container: " + id_);
        }
    }

    if (result > 0) {
        if (WIFEXITED(status)) {
            exit_code_ = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            exit_code_ = 128 + WTERMSIG(status);
            exit_reason_ = "Killed by signal " + std::to_string(WTERMSIG(status));
        }
    }
}

void Container::killProcess(int signal) {
    if (main_pid_.load() > 0) {
        ::kill(main_pid_.load(), signal);
    }
}

void Container::cleanupResources() {
    // TODO: Implement resource cleanup
    logInfo("Cleaning up resources for container: " + id_);
}

void Container::emitEvent(const std::string& event_type,
                         const std::map<std::string, std::string>& event_data) {
    // TODO: Implement event emission when EventManager integration is complete
    logInfo("Event: " + event_type);
}

bool Container::canTransitionTo(ContainerState new_state) const {
    ContainerState current = state_.load();

    // Define valid state transitions
    switch (current) {
        case ContainerState::CREATED:
            return new_state == ContainerState::STARTING ||
                   new_state == ContainerState::REMOVING;
        case ContainerState::STARTING:
            return new_state == ContainerState::RUNNING ||
                   new_state == ContainerState::ERROR ||
                   new_state == ContainerState::REMOVING;
        case ContainerState::RUNNING:
            return new_state == ContainerState::STOPPING ||
                   new_state == ContainerState::PAUSED ||
                   new_state == ContainerState::RESTARTING ||
                   new_state == ContainerState::ERROR ||
                   new_state == ContainerState::REMOVING;
        case ContainerState::PAUSED:
            return new_state == ContainerState::RUNNING ||
                   new_state == ContainerState::STOPPING ||
                   new_state == ContainerState::REMOVING;
        case ContainerState::STOPPING:
            return new_state == ContainerState::STOPPED ||
                   new_state == ContainerState::ERROR ||
                   new_state == ContainerState::REMOVING;
        case ContainerState::STOPPED:
            return new_state == ContainerState::STARTING ||
                   new_state == ContainerState::REMOVING;
        case ContainerState::RESTARTING:
            return new_state == ContainerState::STARTING ||
                   new_state == ContainerState::ERROR ||
                   new_state == ContainerState::REMOVING;
        case ContainerState::ERROR:
            return new_state == ContainerState::STOPPED ||
                   new_state == ContainerState::REMOVING;
        case ContainerState::REMOVING:
            return new_state == ContainerState::REMOVED;
        case ContainerState::REMOVED:
            return false; // Terminal state
        default:
            return false;
    }
}

void Container::handleError(const std::string& error_msg) {
    transitionState(ContainerState::ERROR);
    logError(error_msg);

    emitEvent("container.error", {
        {"container_id", id_},
        {"error", error_msg}
    });
}

void Container::setExitReason(const std::string& reason) {
    exit_reason_ = reason;
}

std::string Container::generateCgroupName() const {
    return "docker-cpp-" + id_;
}

std::string Container::generateLogPrefix() const {
    return "[" + id_ + "] ";
}

void Container::logInfo(const std::string& message) const {
    // TODO: Implement logging when Logger integration is complete
    std::cout << generateLogPrefix() << message << std::endl;
}

void Container::logError(const std::string& message) const {
    // TODO: Implement logging when Logger integration is complete
    std::cerr << generateLogPrefix() << "ERROR: " << message << std::endl;
}

void Container::logWarning(const std::string& message) const {
    // TODO: Implement logging when Logger integration is complete
    std::cout << generateLogPrefix() << "WARNING: " << message << std::endl;
}

} // namespace runtime
} // namespace docker_cpp
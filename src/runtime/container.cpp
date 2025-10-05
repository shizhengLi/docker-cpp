#include "container.hpp"
#include "docker-cpp/core/logger.hpp"
#include "docker-cpp/core/event.hpp"
#include "docker-cpp/plugin/plugin_registry.hpp"
#include "docker-cpp/namespace/namespace_manager.hpp"
#include "docker-cpp/cgroup/cgroup_manager.hpp"
#include "docker-cpp/namespace/process_manager.hpp"

#include <algorithm>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstring>
#include <errno.h>

namespace dockercpp {
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
    , logger_(core::Logger::getInstance())
    , event_manager_(core::EventManager::getInstance())
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
    , process_manager_(std::move(other.process_manager_))
    , main_pid_(other.main_pid_.load())
    , exit_code_(other.exit_code_.load())
    , exit_reason_(std::move(other.exit_reason_))
    , cgroup_manager_(std::move(other.cgroup_manager_))
    , created_at_(other.created_at_)
    , started_at_(other.started_at_)
    , finished_at_(other.finished_at_)
    , monitoring_thread_(std::move(other.monitoring_thread_))
    , monitoring_active_(other.monitoring_active_.load())
    , event_callback_(std::move(other.event_callback_))
    , healthcheck_thread_(std::move(other.healthcheck_thread_))
    , healthcheck_active_(other.healthcheck_active_.load())
    , healthy_(other.healthy_.load())
    , health_status_(std::move(other.health_status_))
    , last_healthcheck_(other.last_healthcheck_)
    , logger_(other.logger_)
    , event_manager_(other.event_manager_)
    , plugin_registry_(other.plugin_registry_) {

    other.logger_ = nullptr;
    other.event_manager_ = nullptr;
    other.plugin_registry_ = nullptr;
    other.state_ = ContainerState::REMOVED;
    other.removed_ = true;
}

Container& Container::operator=(Container&& other) noexcept {
    if (this != &other) {
        cleanup();

        id_ = std::move(other.id_);
        config_ = std::move(other.config_);
        state_ = other.state_.load();
        removed_ = other.removed_.load();
        process_manager_ = std::move(other.process_manager_);
        main_pid_ = other.main_pid_.load();
        exit_code_ = other.exit_code_.load();
        exit_reason_ = std::move(other.exit_reason_);
        cgroup_manager_ = std::move(other.cgroup_manager_);
        created_at_ = other.created_at_;
        started_at_ = other.started_at_;
        finished_at_ = other.finished_at_;
        monitoring_thread_ = std::move(other.monitoring_thread_);
        monitoring_active_ = other.monitoring_active_.load();
        event_callback_ = std::move(other.event_callback_);
        healthcheck_thread_ = std::move(other.healthcheck_thread_);
        healthcheck_active_ = other.healthcheck_active_.load();
        healthy_ = other.healthy_.load();
        health_status_ = std::move(other.health_status_);
        last_healthcheck_ = other.last_healthcheck_;
        logger_ = other.logger_;
        event_manager_ = other.event_manager_;
        plugin_registry_ = other.plugin_registry_;

        other.logger_ = nullptr;
        other.event_manager_ = nullptr;
        other.plugin_registry_ = nullptr;
        other.state_ = ContainerState::REMOVED;
        other.removed_ = true;
    }
    return *this;
}

void Container::start() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!canTransitionTo(ContainerState::STARTING)) {
        throw InvalidContainerStateError(id_, state_.load(), ContainerState::STARTING);
    }

    if (removed_) {
        throw ContainerRuntimeError("Cannot start removed container: " + id_);
    }

    try {
        transitionState(ContainerState::STARTING);

        // Setup namespaces
        setupNamespaces();

        // Setup cgroups
        setupCgroups();

        // Start the container process
        startProcess();

        // Start monitoring
        startMonitoringThread();

        // Start health checking if configured
        if (!config_.healthcheck.test.empty()) {
            startHealthcheckThread();
        }

        transitionState(ContainerState::RUNNING);
        started_at_ = std::chrono::system_clock::now();

        emitEvent("container.started", {
            {"container_id", id_},
            {"pid", std::to_string(main_pid_.load())}
        });

        logInfo("Container started: " + id_);

    } catch (const std::exception& e) {
        transitionState(ContainerState::ERROR);
        handleError("Failed to start container: " + std::string(e.what()));
        throw;
    }
}

void Container::stop(int timeout) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!canTransitionTo(ContainerState::STOPPING)) {
        throw InvalidContainerStateError(id_, state_.load(), ContainerState::STOPPING);
    }

    try {
        transitionState(ContainerState::STOPPING);

        if (main_pid_ > 0) {
            // Send SIGTERM first
            killProcess(SIGTERM);

            // Wait for graceful shutdown
            waitForProcessExit(timeout);

            // If still running, force kill
            if (isProcessRunning()) {
                killProcess(SIGKILL);
                waitForProcessExit(5);
            }
        }

        transitionState(ContainerState::STOPPED);
        finished_at_ = std::chrono::system_clock::now();

        emitEvent("container.stopped", {
            {"container_id", id_},
            {"exit_code", std::to_string(exit_code_.load())}
        });

        logInfo("Container stopped: " + id_);

    } catch (const std::exception& e) {
        transitionState(ContainerState::ERROR);
        handleError("Failed to stop container: " + std::string(e.what()));
        throw;
    }
}

void Container::pause() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!canTransitionTo(ContainerState::PAUSED)) {
        throw InvalidContainerStateError(id_, state_.load(), ContainerState::PAUSED);
    }

    try {
        // TODO: Implement container pause logic
        // This would involve sending SIGSTOP to the process

        transitionState(ContainerState::PAUSED);

        emitEvent("container.paused", {
            {"container_id", id_}
        });

        logInfo("Container paused: " + id_);

    } catch (const std::exception& e) {
        handleError("Failed to pause container: " + std::string(e.what()));
        throw;
    }
}

void Container::resume() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!canTransitionTo(ContainerState::RUNNING)) {
        throw InvalidContainerStateError(id_, state_.load(), ContainerState::RUNNING);
    }

    try {
        // TODO: Implement container resume logic
        // This would involve sending SIGCONT to the process

        transitionState(ContainerState::RUNNING);

        emitEvent("container.resumed", {
            {"container_id", id_}
        });

        logInfo("Container resumed: " + id_);

    } catch (const std::exception& e) {
        handleError("Failed to resume container: " + std::string(e.what()));
        throw;
    }
}

void Container::restart(int timeout) {
    try {
        if (state_ == ContainerState::RUNNING || state_ == ContainerState::PAUSED) {
            stop(timeout);
        }

        // Wait a bit before restart
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        start();

        emitEvent("container.restarted", {
            {"container_id", id_}
        });

        logInfo("Container restarted: " + id_);

    } catch (const std::exception& e) {
        handleError("Failed to restart container: " + std::string(e.what()));
        throw;
    }
}

void Container::remove(bool force) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (removed_) {
        return; // Already removed
    }

    if (!force && (state_ == ContainerState::RUNNING || state_ == ContainerState::PAUSED)) {
        throw InvalidContainerStateError(id_, state_.load(), ContainerState::REMOVED);
    }

    try {
        if (state_ == ContainerState::RUNNING || state_ == ContainerState::PAUSED) {
            stop(5);
        }

        transitionState(ContainerState::REMOVING);

        // Cleanup resources
        cleanupResources();

        transitionState(ContainerState::REMOVED);
        removed_ = true;

        emitEvent("container.removed", {
            {"container_id", id_}
        });

        logInfo("Container removed: " + id_);

    } catch (const std::exception& e) {
        handleError("Failed to remove container: " + std::string(e.what()));
        throw;
    }
}

void Container::kill(int signal) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (main_pid_ <= 0) {
        throw ContainerRuntimeError("Container process not running: " + id_);
    }

    try {
        killProcess(signal);

        emitEvent("container.killed", {
            {"container_id", id_},
            {"signal", std::to_string(signal)}
        });

        logInfo("Container killed: " + id_ + " with signal " + std::to_string(signal));

    } catch (const std::exception& e) {
        handleError("Failed to kill container: " + std::string(e.what()));
        throw;
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
    info.pid = main_pid_.load();
    info.exit_code = exit_code_.load();
    info.error = exit_reason_;
    info.config = config_;
    info.stats = getStats();

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
    std::lock_guard<std::mutex> lock(mutex_);
    return exit_reason_;
}

pid_t Container::getMainProcessPID() const {
    return main_pid_.load();
}

std::vector<pid_t> Container::getAllProcesses() const {
    std::vector<pid_t> processes;

    if (main_pid_ > 0) {
        processes.push_back(main_pid_.load());

        // TODO: Add logic to get child processes
        // This would involve reading /proc/<pid>/task/<tid>/children
    }

    return processes;
}

bool Container::isProcessRunning() const {
    if (main_pid_ <= 0) {
        return false;
    }

    // Check if process is still running
    return kill(main_pid_.load(), 0) == 0;
}

void Container::updateResources(const ResourceLimits& limits) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (state_ != ContainerState::RUNNING && state_ != ContainerState::PAUSED) {
        throw InvalidContainerStateError(id_, state_.load(), ContainerState::RUNNING);
    }

    try {
        // Update resource limits in cgroup manager
        if (cgroup_manager_) {
            if (limits.memory_limit > 0) {
                cgroup_manager_->setMemoryLimit(limits.memory_limit);
            }
            if (limits.cpu_shares != 1.0) {
                cgroup_manager_->setCpuShares(limits.cpu_shares);
            }
            if (limits.cpu_quota > 0) {
                cgroup_manager_->setCpuQuota(limits.cpu_quota, limits.cpu_period);
            }
            if (limits.pids_limit > 0) {
                cgroup_manager_->setPidLimit(limits.pids_limit);
            }
        }

        // Update config
        config_.resources = limits;

        emitEvent("container.resources_updated", {
            {"container_id", id_}
        });

        logInfo("Container resources updated: " + id_);

    } catch (const std::exception& e) {
        handleError("Failed to update container resources: " + std::string(e.what()));
        throw;
    }
}

ResourceStats Container::getStats() const {
    ResourceStats stats;

    try {
        if (cgroup_manager_ && state_ == ContainerState::RUNNING) {
            // Get statistics from cgroup manager
            // This is a placeholder - actual implementation would read from cgroup files
            stats.memory_usage_bytes = 0; // TODO: Read from cgroup
            stats.cpu_usage_percent = 0.0; // TODO: Calculate from cgroup
            stats.current_pids = 1; // TODO: Read from cgroup
        }

        stats.timestamp = std::chrono::system_clock::now();

    } catch (const std::exception& e) {
        logWarning("Failed to get container stats: " + std::string(e.what()));
    }

    return stats;
}

void Container::resetStats() {
    // TODO: Reset statistics counters
    logInfo("Container stats reset: " + id_);
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
    config_.id = id_; // Preserve our ID
}

void Container::startMonitoring() {
    if (!monitoring_active_) {
        startMonitoringThread();
    }
}

void Container::stopMonitoring() {
    stopMonitoringThread();
}

bool Container::isMonitoring() const {
    return monitoring_active_;
}

bool Container::isHealthy() const {
    return healthy_;
}

std::string Container::getHealthStatus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return health_status_;
}

void Container::cleanup() {
    try {
        // Stop monitoring
        stopMonitoringThread();
        stopHealthcheckThread();

        // Cleanup resources
        cleanupResources();

        logInfo("Container cleanup completed: " + id_);

    } catch (const std::exception& e) {
        logError("Error during container cleanup: " + std::string(e.what()));
    }
}

// Private methods
void Container::transitionState(ContainerState new_state) {
    ContainerState old_state = state_.load();
    state_.store(new_state);

    // Notify callback if set
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (event_callback_) {
            try {
                event_callback_(*this, old_state, new_state);
            } catch (const std::exception& e) {
                logWarning("Event callback failed: " + std::string(e.what()));
            }
        }
    }
}

void Container::setupNamespaces() {
    // TODO: Implement namespace setup
    // This would create namespace managers for each required namespace type
    logInfo("Setting up namespaces for container: " + id_);
}

void Container::setupCgroups() {
    try {
        // TODO: Implement cgroup setup when integration is ready
        logInfo("Cgroups setup placeholder for container: " + id_);

    } catch (const std::exception& e) {
        throw ContainerRuntimeError("Failed to setup cgroups: " + std::string(e.what()));
    }
}

void Container::startProcess() {
    try {
        // Create process manager
        process_manager_ = std::make_unique<process::ProcessManager>();

        // Prepare process configuration
        process::ProcessConfig proc_config;
        proc_config.executable = config_.command.empty() ? "/bin/sh" : config_.command[0];
        proc_config.args = config_.args.empty() ? config_.command : config_.args;
        proc_config.env = config_.env;
        proc_config.working_dir = config_.working_dir;
        proc_config.stdin_fd = config_.attach_stdin ? STDIN_FILENO : -1;
        proc_config.stdout_fd = config_.attach_stdout ? STDOUT_FILENO : -1;
        proc_config.stderr_fd = config_.attach_stderr ? STDERR_FILENO : -1;

        // Setup namespace file descriptors
        for (const auto& [type, ns] : namespace_managers_) {
            if (ns && ns->isValid()) {
                proc_config.namespaces.push_back({type, ns->getFileDescriptor()});
            }
        }

        // Start the process
        auto result = process_manager_->createProcess(proc_config);
        if (!result) {
            throw ContainerRuntimeError("Failed to start container process: " + result.error().message);
        }

        main_pid_ = result.value();

        // Add process to cgroup
        if (cgroup_manager_) {
            cgroup_manager_->addProcess(main_pid_);
        }

        logInfo("Container process started: " + id_ + " with PID " + std::to_string(main_pid_));

    } catch (const std::exception& e) {
        throw ContainerRuntimeError("Failed to start container process: " + std::string(e.what()));
    }
}

void Container::monitorProcess() {
    if (main_pid_ <= 0) {
        return;
    }

    try {
        // Wait for process to exit
        int status;
        pid_t result = waitpid(main_pid_, &status, WNOHANG);

        if (result > 0) {
            // Process has exited
            if (WIFEXITED(status)) {
                exit_code_ = WEXITSTATUS(status);
                setExitReason("Process exited normally");
            } else if (WIFSIGNALED(status)) {
                exit_code_ = -1;
                setExitReason("Process killed by signal: " + std::to_string(WTERMSIG(status)));
            }

            if (state_ == ContainerState::RUNNING) {
                transitionState(ContainerState::STOPPED);
                finished_at_ = std::chrono::system_clock::now();

                emitEvent("container.exited", {
                    {"container_id", id_},
                    {"exit_code", std::to_string(exit_code_.load())}
                });

                logInfo("Container process exited: " + id_ + " with exit code " + std::to_string(exit_code_.load()));
            }
        }

    } catch (const std::exception& e) {
        logError("Error monitoring container process: " + std::string(e.what()));
    }
}

void Container::startMonitoringThread() {
    if (monitoring_active_) {
        return;
    }

    monitoring_active_ = true;
    monitoring_thread_ = std::make_unique<std::thread>([this]() {
        while (monitoring_active_ && !removed_) {
            try {
                monitorProcess();

                // Sleep before next check
                std::unique_lock<std::mutex> lock(monitoring_mutex_);
                monitoring_cv_.wait_for(lock, std::chrono::seconds(1), [this]() {
                    return !monitoring_active_;
                });

            } catch (const std::exception& e) {
                logError("Monitoring thread error: " + std::string(e.what()));
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    });

    logInfo("Monitoring thread started for container: " + id_);
}

void Container::stopMonitoringThread() {
    monitoring_active_ = false;
    monitoring_cv_.notify_all();

    if (monitoring_thread_ && monitoring_thread_->joinable()) {
        monitoring_thread_->join();
    }

    logInfo("Monitoring thread stopped for container: " + id_);
}

void Container::startHealthcheckThread() {
    if (healthcheck_active_ || config_.healthcheck.test.empty()) {
        return;
    }

    healthcheck_active_ = true;
    healthcheck_thread_ = std::make_unique<std::thread>([this]() {
        while (healthcheck_active_ && !removed_ && state_ == ContainerState::RUNNING) {
            try {
                executeHealthcheck();

                // Wait for next health check
                std::this_thread::sleep_for(std::chrono::seconds(config_.healthcheck.interval));

            } catch (const std::exception& e) {
                logError("Health check error: " + std::string(e.what()));
                std::this_thread::sleep_for(std::chrono::seconds(config_.healthcheck.interval));
            }
        }
    });

    logInfo("Health check thread started for container: " + id_);
}

void Container::stopHealthcheckThread() {
    healthcheck_active_ = false;

    if (healthcheck_thread_ && healthcheck_thread_->joinable()) {
        healthcheck_thread_->join();
    }

    logInfo("Health check thread stopped for container: " + id_);
}

void Container::executeHealthcheck() {
    // TODO: Implement health check execution
    // This would run the configured health check command and update the health status
    last_healthcheck_ = std::chrono::system_clock::now();
}

void Container::waitForProcessExit(int timeout) {
    if (main_pid_ <= 0) {
        return;
    }

    auto start_time = std::chrono::steady_clock::now();
    auto timeout_duration = std::chrono::seconds(timeout);

    while (std::chrono::steady_clock::now() - start_time < timeout_duration) {
        if (!isProcessRunning()) {
            monitorProcess(); // Update exit status
            return;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    logWarning("Timeout waiting for container process to exit: " + id_);
}

void Container::killProcess(int signal) {
    if (main_pid_ <= 0) {
        return;
    }

    if (kill(main_pid_, signal) != 0) {
        throw ContainerRuntimeError("Failed to send signal " + std::to_string(signal) +
                                   " to process " + std::to_string(main_pid_) +
                                   ": " + std::string(strerror(errno)));
    }
}

void Container::cleanupResources() {
    try {
        // Stop monitoring
        stopMonitoringThread();
        stopHealthcheckThread();

        // Cleanup cgroup
        if (cgroup_manager_) {
            cgroup_manager_.reset();
        }

        // Cleanup namespaces
        namespace_managers_.clear();

        // Cleanup process manager
        if (process_manager_) {
            process_manager_.reset();
        }

        logInfo("Resource cleanup completed for container: " + id_);

    } catch (const std::exception& e) {
        logError("Error during resource cleanup: " + std::string(e.what()));
    }
}

void Container::emitEvent(const std::string& event_type,
                         const std::map<std::string, std::string>& event_data) {
    if (event_manager_) {
        try {
            Event event(event_type, event_data);
            event_manager_->publish(event);
        } catch (const std::exception& e) {
            logWarning("Failed to emit event: " + std::string(e.what()));
        }
    }
}

bool Container::canTransitionTo(ContainerState new_state) const {
    ContainerState current = state_.load();

    // Define valid state transitions
    switch (current) {
        case ContainerState::CREATED:
            return new_state == ContainerState::STARTING || new_state == ContainerState::REMOVING;

        case ContainerState::STARTING:
            return new_state == ContainerState::RUNNING || new_state == ContainerState::ERROR;

        case ContainerState::RUNNING:
            return new_state == ContainerState::PAUSED || new_state == ContainerState::STOPPING ||
                   new_state == ContainerState::RESTARTING || new_state == ContainerState::ERROR;

        case ContainerState::PAUSED:
            return new_state == ContainerState::RUNNING || new_state == ContainerState::STOPPING ||
                   new_state == ContainerState::REMOVING;

        case ContainerState::STOPPING:
            return new_state == ContainerState::STOPPED || new_state == ContainerState::ERROR;

        case ContainerState::STOPPED:
            return new_state == ContainerState::RESTARTING || new_state == ContainerState::REMOVING;

        case ContainerState::RESTARTING:
            return new_state == ContainerState::STARTING || new_state == ContainerState::ERROR;

        case ContainerState::REMOVING:
            return new_state == ContainerState::REMOVED || new_state == ContainerState::ERROR;

        case ContainerState::REMOVED:
            return false; // Terminal state

        case ContainerState::DEAD:
        case ContainerState::ERROR:
            return new_state == ContainerState::REMOVING;

        default:
            return false;
    }
}

std::vector<ContainerState> Container::getValidTransitions(ContainerState from_state) const {
    std::vector<ContainerState> transitions;

    switch (from_state) {
        case ContainerState::CREATED:
            transitions = {ContainerState::STARTING, ContainerState::REMOVING};
            break;
        case ContainerState::STARTING:
            transitions = {ContainerState::RUNNING, ContainerState::ERROR};
            break;
        case ContainerState::RUNNING:
            transitions = {ContainerState::PAUSED, ContainerState::STOPPING,
                          ContainerState::RESTARTING, ContainerState::ERROR};
            break;
        case ContainerState::PAUSED:
            transitions = {ContainerState::RUNNING, ContainerState::STOPPING, ContainerState::REMOVING};
            break;
        case ContainerState::STOPPING:
            transitions = {ContainerState::STOPPED, ContainerState::ERROR};
            break;
        case ContainerState::STOPPED:
            transitions = {ContainerState::RESTARTING, ContainerState::REMOVING};
            break;
        case ContainerState::RESTARTING:
            transitions = {ContainerState::STARTING, ContainerState::ERROR};
            break;
        case ContainerState::REMOVING:
            transitions = {ContainerState::REMOVED, ContainerState::ERROR};
            break;
        case ContainerState::DEAD:
        case ContainerState::ERROR:
            transitions = {ContainerState::REMOVING};
            break;
        case ContainerState::REMOVED:
            break; // Terminal state
    }

    return transitions;
}

void Container::handleError(const std::string& error_msg) {
    setExitReason(error_msg);
    logError(error_msg);

    emitEvent("container.error", {
        {"container_id", id_},
        {"error", error_msg}
    });
}

void Container::setExitReason(const std::string& reason) {
    std::lock_guard<std::mutex> lock(mutex_);
    exit_reason_ = reason;
}

std::string Container::generateCgroupName() const {
    return "docker-cpp-" + id_;
}

std::string Container::generateLogPrefix() const {
    return "Container[" + id_ + "]";
}

void Container::logInfo(const std::string& message) const {
    if (logger_) {
        logger_->info(generateLogPrefix(), message);
    }
}

void Container::logError(const std::string& message) const {
    if (logger_) {
        logger_->error(generateLogPrefix(), message);
    }
}

void Container::logWarning(const std::string& message) const {
    if (logger_) {
        logger_->warning(generateLogPrefix(), message);
    }
}

} // namespace runtime
} // namespace dockercpp
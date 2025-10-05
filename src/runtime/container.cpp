#include "container.hpp"
#include <docker-cpp/cgroup/cgroup_manager.hpp>
#include <docker-cpp/core/event.hpp>
#include <docker-cpp/core/logger.hpp>
#include <docker-cpp/namespace/namespace_manager.hpp>
#include <docker-cpp/namespace/process_manager.hpp>
#include <docker-cpp/plugin/plugin_registry.hpp>

#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <thread>

namespace docker_cpp {
namespace runtime {

// Container implementation
Container::Container(const ContainerConfig& config)
    : id_(config.id.empty() ? generateContainerId() : config.id), config_(config),
      state_(ContainerState::CREATED), removed_(false), main_pid_(0), exit_code_(0),
      created_at_(std::chrono::system_clock::now()), monitoring_active_(false),
      healthcheck_active_(false), healthy_(true), health_status_("healthy"), logger_(nullptr),
      event_manager_(nullptr), plugin_registry_(nullptr)
{

    // Update config with our generated ID
    config_.id = id_;

    // Set default timestamps
    started_at_ = created_at_;
    finished_at_ = created_at_;

    logInfo("Container created: " + id_);
    emitEvent("container.created",
              {{"container_id", id_}, {"image", config_.image}, {"name", config_.name}});
}

Container::~Container()
{
    cleanup();
}

Container::Container(Container&& other) noexcept
    : id_(std::move(other.id_)), config_(std::move(other.config_)), state_(other.state_.load()),
      removed_(other.removed_.load()), main_pid_(other.main_pid_.load()),
      exit_code_(other.exit_code_.load()), exit_reason_(std::move(other.exit_reason_)),
      created_at_(other.created_at_), started_at_(other.started_at_),
      finished_at_(other.finished_at_), monitoring_active_(other.monitoring_active_.load()),
      healthy_(other.healthy_.load()), health_status_(std::move(other.health_status_)),
      last_healthcheck_(other.last_healthcheck_), logger_(other.logger_),
      event_manager_(other.event_manager_), plugin_registry_(other.plugin_registry_)
{

    // Don't move mutex - it can't be moved
    other.monitoring_active_ = false;
    other.healthcheck_active_ = false;
}

Container& Container::operator=(Container&& other) noexcept
{
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

void Container::start()
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!canTransitionTo(ContainerState::STARTING)) {
        throw InvalidContainerStateError(id_, state_.load(), ContainerState::STARTING);
    }

    try {
        transitionState(ContainerState::STARTING);

        // The state machine will handle process start and transition to RUNNING
        if (main_pid_.load() <= 0) {
            throw ContainerRuntimeError("Failed to start container process");
        }

        transitionState(ContainerState::RUNNING);

        emitEvent("container.started",
                  {{"container_id", id_}, {"pid", std::to_string(main_pid_.load())}});

        logInfo("Container started successfully: " + id_);
    }
    catch (const std::exception& e) {
        transitionState(ContainerState::ERROR);
        logError("Failed to start container: " + std::string(e.what()));
        throw;
    }
}

void Container::stop(int timeout)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!canTransitionTo(ContainerState::STOPPING)) {
        throw InvalidContainerStateError(id_, state_.load(), ContainerState::STOPPING);
    }

    try {
        // Send SIGTERM first if process is running
        if (main_pid_.load() > 0) {
            ::kill(main_pid_.load(), SIGTERM);
        }

        transitionState(ContainerState::STOPPING);

        // Wait for process exit with timeout
        if (main_pid_.load() > 0) {
            int elapsed = 0;
            while (isProcessRunning() && elapsed < timeout) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                elapsed++;
            }

            // Force kill if still running
            if (isProcessRunning()) {
                ::kill(main_pid_.load(), SIGKILL);
                waitForProcessExit(5);
            }
        }

        transitionState(ContainerState::STOPPED);

        emitEvent("container.stopped",
                  {{"container_id", id_}, {"exit_code", std::to_string(exit_code_.load())}});

        logInfo("Container stopped successfully: " + id_);
    }
    catch (const std::exception& e) {
        transitionState(ContainerState::ERROR);
        logError("Failed to stop container: " + std::string(e.what()));
        throw;
    }
}

void Container::pause()
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!canTransitionTo(ContainerState::PAUSED)) {
        throw InvalidContainerStateError(id_, state_.load(), ContainerState::PAUSED);
    }

    transitionState(ContainerState::PAUSED);
    logInfo("Container paused: " + id_);

    emitEvent("container.paused", {{"container_id", id_}});
}

void Container::resume()
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!canTransitionTo(ContainerState::RUNNING)) {
        throw InvalidContainerStateError(id_, state_.load(), ContainerState::RUNNING);
    }

    transitionState(ContainerState::RUNNING);
    logInfo("Container resumed: " + id_);

    emitEvent("container.resumed", {{"container_id", id_}});
}

void Container::restart(int timeout)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!canTransitionTo(ContainerState::RESTARTING)) {
        throw InvalidContainerStateError(id_, state_.load(), ContainerState::RESTARTING);
    }

    try {
        transitionState(ContainerState::RESTARTING);
        logInfo("Restarting container: " + id_);

        // Stop the container first
        if (state_.load() == ContainerState::RUNNING) {
            // Temporarily unlock for stop operation
            lock.~lock_guard();
            try {
                stop(timeout);
            }
            catch (...) {
                // Re-acquire lock before re-throwing
                new (&lock) std::lock_guard<std::mutex>(mutex_);
                throw;
            }
            // Re-acquire lock
            new (&lock) std::lock_guard<std::mutex>(mutex_);
        }

        // Start the container again
        if (state_.load() == ContainerState::STOPPED) {
            // Temporarily unlock for start operation
            lock.~lock_guard();
            try {
                start();
            }
            catch (...) {
                // Re-acquire lock before re-throwing
                new (&lock) std::lock_guard<std::mutex>(mutex_);
                throw;
            }
            // Re-acquire lock
            new (&lock) std::lock_guard<std::mutex>(mutex_);
        }

        logInfo("Container restarted successfully: " + id_);
    }
    catch (const std::exception& e) {
        transitionState(ContainerState::ERROR);
        logError("Failed to restart container: " + std::string(e.what()));
        throw;
    }
}

void Container::remove(bool force)
{
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
    }
    catch (const std::exception& e) {
        transitionState(ContainerState::ERROR);
        logError("Failed to remove container: " + std::string(e.what()));
        throw;
    }
}

void Container::kill(int signal)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (main_pid_.load() > 0) {
        logInfo("Killing container " + id_ + " with signal " + std::to_string(signal));
        ::kill(main_pid_.load(), signal);
    }
}

ContainerState Container::getState() const
{
    return state_.load();
}

ContainerInfo Container::getInfo() const
{
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

std::chrono::system_clock::time_point Container::getStartTime() const
{
    return started_at_;
}

std::chrono::system_clock::time_point Container::getFinishedTime() const
{
    return finished_at_;
}

int Container::getExitCode() const
{
    return exit_code_.load();
}

std::string Container::getExitReason() const
{
    return exit_reason_;
}

pid_t Container::getMainProcessPID() const
{
    return main_pid_.load();
}

std::vector<pid_t> Container::getAllProcesses() const
{
    if (main_pid_.load() > 0) {
        return {main_pid_.load()};
    }
    return {};
}

bool Container::isProcessRunning() const
{
    pid_t pid = main_pid_.load();
    if (pid <= 0)
        return false;

    return ::kill(pid, 0) == 0;
}

void Container::updateResources(const ResourceLimits& limits)
{
    (void)limits; // Suppress unused parameter warning
    std::lock_guard<std::mutex> lock(mutex_);

    if (state_.load() != ContainerState::RUNNING) {
        throw InvalidContainerStateError(id_, state_.load(), ContainerState::RUNNING);
    }

    // TODO: Implement resource updates when cgroup integration is complete
    logInfo("Resource limits updated for container: " + id_);
}

ResourceStats Container::getStats() const
{
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

void Container::resetStats()
{
    // TODO: Implement statistics reset
    logInfo("Statistics reset for container: " + id_);
}

void Container::setEventCallback(ContainerEventCallback callback)
{
    std::lock_guard<std::mutex> lock(callback_mutex_);
    event_callback_ = callback;
}

void Container::removeEventCallback()
{
    std::lock_guard<std::mutex> lock(callback_mutex_);
    event_callback_ = nullptr;
}

const ContainerConfig& Container::getConfig() const
{
    return config_;
}

void Container::updateConfig(const ContainerConfig& config)
{
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
}

void Container::startMonitoring()
{
    if (monitoring_active_.load())
        return;

    monitoring_active_ = true;
    startMonitoringThread();
}

void Container::stopMonitoring()
{
    monitoring_active_ = false;
    stopMonitoringThread();
}

bool Container::isMonitoring() const
{
    return monitoring_active_.load();
}

bool Container::isHealthy() const
{
    return healthy_.load();
}

std::string Container::getHealthStatus() const
{
    return health_status_;
}

void Container::cleanup()
{
    if (removed_.load())
        return;

    try {
        stopMonitoring();
        stopHealthcheckThread();
        cleanupResources();
        removed_ = true;
    }
    catch (const std::exception& e) {
        logError("Error during cleanup: " + std::string(e.what()));
    }
}

// Private methods
void Container::transitionState(ContainerState new_state)
{
    ContainerState old_state = state_.load();

    if (!isStateTransitionValid(old_state, new_state)) {
        throw InvalidContainerStateError(id_, old_state, new_state);
    }

    // Exit the old state
    onStateExited(old_state);

    // Execute the transition
    executeStateTransition(new_state);

    // Enter the new state
    state_.store(new_state);
    onStateEntered(new_state);

    logStateTransition(old_state, new_state);

    // Notify event callback if set
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (event_callback_) {
            try {
                event_callback_(*this, old_state, new_state);
            }
            catch (const std::exception& e) {
                logError("Event callback failed: " + std::string(e.what()));
            }
        }
    }
}

void Container::setupNamespaces()
{
    // TODO: Implement namespace setup using Phase 1 components
    logInfo("Setting up namespaces for container: " + id_);
}

void Container::setupCgroups()
{
    // TODO: Implement cgroup setup using Phase 1 components
    logInfo("Setting up cgroups for container: " + id_);
}

void Container::startProcess()
{
    // For now, create a simple sleep process as placeholder
    pid_t pid = fork();

    if (pid == -1) {
        throw ContainerRuntimeError("Failed to fork process: " + std::string(strerror(errno)));
    }
    else if (pid == 0) {
        // Child process
        try {
            // Set up environment
            for (const auto& env_var : config_.env) {
                putenv(strdup(env_var.c_str()));
            }

            // Change working directory if specified
            if (!config_.working_dir.empty()) {
                if (chdir(config_.working_dir.c_str()) != 0) {
                    logError("Failed to change working directory to " + config_.working_dir);
                }
            }

            // Execute the command (simple sleep for now)
            execl("/bin/sleep", "sleep", "infinity", nullptr);

            // If we get here, exec failed
            _exit(127);
        }
        catch (...) {
            _exit(127);
        }
    }
    else {
        // Parent process
        main_pid_ = pid;
        logInfo("Started process with PID " + std::to_string(pid) + " for container: " + id_);

        // Give the process a moment to start
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Check if process is still alive
        if (!isProcessRunning()) {
            int status;
            waitpid(pid, &status, WNOHANG);
            if (WIFEXITED(status)) {
                exit_code_ = WEXITSTATUS(status);
                throw ContainerRuntimeError("Process exited immediately with code "
                                            + std::to_string(exit_code_.load()));
            }
            else if (WIFSIGNALED(status)) {
                throw ContainerRuntimeError("Process killed by signal "
                                            + std::to_string(WTERMSIG(status)));
            }
            throw ContainerRuntimeError("Process failed to start");
        }
    }
}

void Container::monitorProcess()
{
    // TODO: Implement process monitoring
    logInfo("Monitoring process for container: " + id_);
}

void Container::startMonitoringThread()
{
    // TODO: Implement monitoring thread
    logInfo("Starting monitoring thread for container: " + id_);
}

void Container::stopMonitoringThread()
{
    // TODO: Implement monitoring thread cleanup
    logInfo("Stopping monitoring thread for container: " + id_);
}

void Container::startHealthcheckThread()
{
    // TODO: Implement health check thread
    logInfo("Starting health check thread for container: " + id_);
}

void Container::stopHealthcheckThread()
{
    // TODO: Implement health check thread cleanup
    logInfo("Stopping health check thread for container: " + id_);
}

void Container::executeHealthcheck()
{
    // TODO: Implement health check execution
    healthy_ = true;
    health_status_ = "healthy";
    last_healthcheck_ = std::chrono::system_clock::now();
}

void Container::waitForProcessExit(int timeout)
{
    if (main_pid_.load() <= 0)
        return;

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
        }
        else if (WIFSIGNALED(status)) {
            exit_code_ = 128 + WTERMSIG(status);
            exit_reason_ = "Killed by signal " + std::to_string(WTERMSIG(status));
        }
    }
}

void Container::killProcess(int signal)
{
    if (main_pid_.load() > 0) {
        ::kill(main_pid_.load(), signal);
    }
}

void Container::cleanupResources()
{
    // TODO: Implement resource cleanup
    logInfo("Cleaning up resources for container: " + id_);
}

void Container::emitEvent(const std::string& event_type,
                          const std::map<std::string, std::string>& event_data)
{
    (void)event_data; // Suppress unused parameter warning
    // TODO: Implement event emission when EventManager integration is complete
    logInfo("Event: " + event_type);
}

void Container::initializeStateMachine()
{
    // Initialize any state machine specific resources
    logInfo("State machine initialized for container: " + id_);
}

void Container::executeStateTransition(ContainerState new_state)
{
    // Execute actions specific to the transition
    switch (new_state) {
        case ContainerState::STARTING:
            setupNamespaces();
            setupCgroups();
            startProcess();
            break;

        case ContainerState::STOPPING:
            if (main_pid_.load() > 0) {
                waitForProcessExit(10); // Default 10 second timeout
            }
            break;

        case ContainerState::REMOVING:
            cleanupResources();
            break;

        case ContainerState::RESTARTING:
            // Restart is essentially stop -> start
            if (main_pid_.load() > 0) {
                waitForProcessExit(5);
            }
            break;

        default:
            break;
    }
}

void Container::onStateEntered(ContainerState new_state)
{
    switch (new_state) {
        case ContainerState::CREATED:
            handleCreatedState();
            break;
        case ContainerState::STARTING:
            handleStartingState();
            break;
        case ContainerState::RUNNING:
            handleRunningState();
            break;
        case ContainerState::PAUSED:
            handlePausedState();
            break;
        case ContainerState::STOPPING:
            handleStoppingState();
            break;
        case ContainerState::STOPPED:
            handleStoppedState();
            break;
        case ContainerState::REMOVING:
            handleRemovingState();
            break;
        case ContainerState::REMOVED:
            handleRemovedState();
            break;
        case ContainerState::DEAD:
            handleDeadState();
            break;
        case ContainerState::RESTARTING:
            handleRestartingState();
            break;
        case ContainerState::ERROR:
            handleErrorState();
            break;
    }
}

void Container::onStateExited(ContainerState old_state)
{
    switch (old_state) {
        case ContainerState::RUNNING:
            // Stop monitoring when leaving running state
            stopMonitoring();
            break;
        case ContainerState::CREATED:
        case ContainerState::STARTING:
        case ContainerState::PAUSED:
        case ContainerState::STOPPING:
        case ContainerState::STOPPED:
        case ContainerState::REMOVING:
        case ContainerState::REMOVED:
        case ContainerState::DEAD:
        case ContainerState::RESTARTING:
        case ContainerState::ERROR:
            // Most states don't need special exit handling
            break;
    }
}

void Container::handleCreatedState()
{
    logInfo("Container created");
    created_at_ = std::chrono::system_clock::now();
}

void Container::handleStartingState()
{
    logInfo("Container starting");
    // Process should already be started in executeStateTransition
}

void Container::handleRunningState()
{
    logInfo("Container running");
    started_at_ = std::chrono::system_clock::now();
    startMonitoring();
    startHealthcheckThread();
}

void Container::handlePausedState()
{
    logInfo("Container paused");
    // TODO: Implement actual pause functionality
}

void Container::handleStoppingState()
{
    logInfo("Container stopping");
    // Process termination handled in executeStateTransition
}

void Container::handleStoppedState()
{
    logInfo("Container stopped");
    finished_at_ = std::chrono::system_clock::now();
    stopHealthcheckThread();
}

void Container::handleRemovingState()
{
    logInfo("Container removing");
    // Cleanup handled in executeStateTransition
}

void Container::handleRemovedState()
{
    logInfo("Container removed");
    removed_.store(true);
}

void Container::handleDeadState()
{
    logInfo("Container dead - unexpected termination");
    finished_at_ = std::chrono::system_clock::now();
    setExitReason("Container died unexpectedly");
    stopMonitoring();
    stopHealthcheckThread();
}

void Container::handleRestartingState()
{
    logInfo("Container restarting");
    // Will transition to STARTING after restart actions complete
}

void Container::handleErrorState()
{
    logError("Container entered error state");
    finished_at_ = std::chrono::system_clock::now();
    stopMonitoring();
    stopHealthcheckThread();
}

bool Container::isStateTransitionValid(ContainerState from, ContainerState to) const
{
    auto transitions = getStateTransitionTable();
    for (const auto& transition : transitions) {
        if (transition.from == from && transition.to == to) {
            if (transition.condition) {
                return transition.condition();
            }
            return transition.allowed;
        }
    }
    return false;
}

std::vector<Container::StateTransition> Container::getStateTransitionTable() const
{
    return {
        // FROM -> TO transitions with conditions
        {ContainerState::CREATED,
         ContainerState::STARTING,
         true,
         [this]() { return !removed_.load(); }},
        {ContainerState::CREATED, ContainerState::REMOVING, true, []() { return true; }},
        {ContainerState::CREATED, ContainerState::ERROR, true, []() { return true; }},

        {ContainerState::STARTING,
         ContainerState::RUNNING,
         true,
         [this]() { return main_pid_.load() > 0; }},
        {ContainerState::STARTING, ContainerState::STOPPING, true, []() { return true; }},
        {ContainerState::STARTING, ContainerState::ERROR, true, []() { return true; }},
        {ContainerState::STARTING, ContainerState::REMOVING, true, []() { return true; }},

        {ContainerState::RUNNING,
         ContainerState::PAUSED,
         true,
         [this]() { return main_pid_.load() > 0; }},
        {ContainerState::RUNNING, ContainerState::STOPPING, true, []() { return true; }},
        {ContainerState::RUNNING, ContainerState::RESTARTING, true, []() { return true; }},
        {ContainerState::RUNNING, ContainerState::ERROR, true, []() { return true; }},
        {ContainerState::RUNNING, ContainerState::REMOVING, true, []() { return true; }},

        {ContainerState::PAUSED,
         ContainerState::RUNNING,
         true,
         [this]() { return main_pid_.load() > 0; }},
        {ContainerState::PAUSED, ContainerState::STOPPING, true, []() { return true; }},
        {ContainerState::PAUSED, ContainerState::REMOVING, true, []() { return true; }},

        {ContainerState::STOPPING, ContainerState::STOPPED, true, []() { return true; }},
        {ContainerState::STOPPING, ContainerState::DEAD, true, []() { return true; }},
        {ContainerState::STOPPING, ContainerState::ERROR, true, []() { return true; }},
        {ContainerState::STOPPING, ContainerState::REMOVING, true, []() { return true; }},

        {ContainerState::STOPPED,
         ContainerState::STARTING,
         true,
         [this]() { return !removed_.load(); }},
        {ContainerState::STOPPED, ContainerState::REMOVING, true, []() { return true; }},
        {ContainerState::STOPPED,
         ContainerState::RESTARTING,
         true,
         [this]() { return !removed_.load(); }},

        {ContainerState::RESTARTING,
         ContainerState::STARTING,
         true,
         [this]() { return !removed_.load(); }},
        {ContainerState::RESTARTING, ContainerState::STOPPING, true, []() { return true; }},
        {ContainerState::RESTARTING, ContainerState::ERROR, true, []() { return true; }},
        {ContainerState::RESTARTING, ContainerState::REMOVING, true, []() { return true; }},

        {ContainerState::ERROR, ContainerState::STOPPED, true, []() { return true; }},
        {ContainerState::ERROR, ContainerState::REMOVING, true, []() { return true; }},

        {ContainerState::REMOVING, ContainerState::REMOVED, true, []() { return true; }},
        {ContainerState::REMOVING, ContainerState::ERROR, true, []() { return true; }}

        // REMOVED, DEAD are terminal states - no outgoing transitions
    };
}

bool Container::canTransitionTo(ContainerState new_state) const
{
    ContainerState current = state_.load();
    return isStateTransitionValid(current, new_state);
}

std::vector<ContainerState> Container::getValidTransitions(ContainerState from_state) const
{
    std::vector<ContainerState> valid_transitions;
    auto transitions = getStateTransitionTable();

    for (const auto& transition : transitions) {
        if (transition.from == from_state) {
            if (transition.condition) {
                if (transition.condition()) {
                    valid_transitions.push_back(transition.to);
                }
            }
            else if (transition.allowed) {
                valid_transitions.push_back(transition.to);
            }
        }
    }

    return valid_transitions;
}

void Container::handleError(const std::string& error_msg)
{
    transitionState(ContainerState::ERROR);
    logError(error_msg);

    emitEvent("container.error", {{"container_id", id_}, {"error", error_msg}});
}

void Container::setExitReason(const std::string& reason)
{
    exit_reason_ = reason;
}

std::string Container::generateCgroupName() const
{
    return "docker-cpp-" + id_;
}

std::string Container::generateLogPrefix() const
{
    return "[" + id_ + "] ";
}

void Container::logInfo(const std::string& message) const
{
    // TODO: Implement logging when Logger integration is complete
    std::cout << generateLogPrefix() << message << std::endl;
}

void Container::logError(const std::string& message) const
{
    // TODO: Implement logging when Logger integration is complete
    std::cerr << generateLogPrefix() << "ERROR: " << message << std::endl;
}

void Container::logWarning(const std::string& message) const
{
    // TODO: Implement logging when Logger integration is complete
    std::cout << generateLogPrefix() << "WARNING: " << message << std::endl;
}

void Container::logDebug(const std::string& message) const
{
    // TODO: Implement debug logging when Logger integration is complete
    std::cout << generateLogPrefix() << "DEBUG: " << message << std::endl;
}

void Container::logStateTransition(ContainerState from, ContainerState to) const
{
    logInfo("State transition: " + getStateDescription(from) + " -> " + getStateDescription(to));
}

std::string Container::getStateDescription(ContainerState state) const
{
    switch (state) {
        case ContainerState::CREATED:
            return "CREATED";
        case ContainerState::STARTING:
            return "STARTING";
        case ContainerState::RUNNING:
            return "RUNNING";
        case ContainerState::PAUSED:
            return "PAUSED";
        case ContainerState::STOPPING:
            return "STOPPING";
        case ContainerState::STOPPED:
            return "STOPPED";
        case ContainerState::REMOVING:
            return "REMOVING";
        case ContainerState::REMOVED:
            return "REMOVED";
        case ContainerState::DEAD:
            return "DEAD";
        case ContainerState::RESTARTING:
            return "RESTARTING";
        case ContainerState::ERROR:
            return "ERROR";
        default:
            return "UNKNOWN";
    }
}

} // namespace runtime
} // namespace docker_cpp
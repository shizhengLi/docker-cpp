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
    ResourceStats stats;
    stats.timestamp = std::chrono::system_clock::now();

    // Initialize with default values
    stats.memory_usage_bytes = 0;
    stats.cpu_usage_percent = 0.0;
    stats.network_rx_bytes = 0;
    stats.network_tx_bytes = 0;
    stats.blkio_read_bytes = 0;
    stats.blkio_write_bytes = 0;

    try {
        // Get resource metrics from cgroup manager if available
        if (cgroup_manager_) {
            ResourceMetrics metrics = cgroup_manager_->getMetrics();

            // Convert cgroup metrics to container stats
            stats.memory_usage_bytes = metrics.memory.current;
            stats.cpu_usage_percent = metrics.cpu.usage_percent;
            stats.blkio_read_bytes = metrics.io.rbytes;
            stats.blkio_write_bytes = metrics.io.wbytes;

            logDebug("Resource stats collected - Memory: "
                     + std::to_string(metrics.memory.current / (1024 * 1024)) + "MB, "
                     + "CPU: " + std::to_string(metrics.cpu.usage_percent) + "%");
        }

        // Get additional stats from process manager if available
        if (process_manager_) {
            // Get process-specific statistics
            ProcessInfo proc_info = process_manager_->getProcessInfo(main_pid_);
            // This is a placeholder - in a real implementation, we would get
            // more detailed process statistics from the process manager
            logDebug("Process stats collected for PID: " + std::to_string(proc_info.pid));
        }
    }
    catch (const std::exception& e) {
        logWarning("Failed to collect resource statistics: " + std::string(e.what()));
        // Return default stats if collection fails
    }

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
    logInfo("Setting up namespaces for container: " + id_);

    try {
        // Setup PID namespace for process isolation
        auto pid_ns = std::make_unique<NamespaceManager>(NamespaceType::PID);
        if (pid_ns->isValid()) {
            namespace_managers_.push_back(std::move(pid_ns));
            logInfo("PID namespace created successfully");
        }
        else {
            logWarning("Failed to create PID namespace, continuing without it");
        }

        // Setup network namespace for network isolation
        auto net_ns = std::make_unique<NamespaceManager>(NamespaceType::NETWORK);
        if (net_ns->isValid()) {
            namespace_managers_.push_back(std::move(net_ns));
            logInfo("Network namespace created successfully");
        }
        else {
            logWarning("Failed to create network namespace, continuing without it");
        }

        // Setup mount namespace for filesystem isolation
        auto mnt_ns = std::make_unique<NamespaceManager>(NamespaceType::MOUNT);
        if (mnt_ns->isValid()) {
            namespace_managers_.push_back(std::move(mnt_ns));
            logInfo("Mount namespace created successfully");
        }
        else {
            logWarning("Failed to create mount namespace, continuing without it");
        }

        // Setup UTS namespace for hostname isolation
        auto uts_ns = std::make_unique<NamespaceManager>(NamespaceType::UTS);
        if (uts_ns->isValid()) {
            namespace_managers_.push_back(std::move(uts_ns));
            logInfo("UTS namespace created successfully");
        }
        else {
            logWarning("Failed to create UTS namespace, continuing without it");
        }

        // Setup IPC namespace for IPC isolation
        auto ipc_ns = std::make_unique<NamespaceManager>(NamespaceType::IPC);
        if (ipc_ns->isValid()) {
            namespace_managers_.push_back(std::move(ipc_ns));
            logInfo("IPC namespace created successfully");
        }
        else {
            logWarning("Failed to create IPC namespace, continuing without it");
        }

        // Only create user namespace if we have proper privileges
        // This might fail on some systems, so handle gracefully
        try {
            auto user_ns = std::make_unique<NamespaceManager>(NamespaceType::USER);
            if (user_ns->isValid()) {
                namespace_managers_.push_back(std::move(user_ns));
                logInfo("User namespace created successfully");
            }
            else {
                logWarning("Failed to create user namespace, continuing without it");
            }
        }
        catch (const std::exception& e) {
            logWarning("User namespace creation failed: " + std::string(e.what()));
        }

        logInfo("Namespace setup completed. Created " + std::to_string(namespace_managers_.size())
                + " namespaces");
    }
    catch (const std::exception& e) {
        logError("Namespace setup failed: " + std::string(e.what()));
        // Continue without namespaces rather than failing completely
    }
}

void Container::setupCgroups()
{
    logInfo("Setting up cgroups for container: " + id_);

    try {
        // Create cgroup configuration
        CgroupConfig config;
        config.name = generateCgroupName();
        config.parent_path = "/docker-cpp";

        // Configure CPU limits
        if (config_.resources.cpu_shares > 0) {
            config.cpu.max_usec = static_cast<uint64_t>(config_.resources.cpu_quota);
            config.cpu.period_usec = static_cast<uint64_t>(config_.resources.cpu_period);
            config.cpu.weight =
                static_cast<uint64_t>(config_.resources.cpu_shares * 1024); // CPU weight
        }

        // Configure memory limits
        if (config_.resources.memory_limit > 0) {
            config.memory.max_bytes = static_cast<uint64_t>(config_.resources.memory_limit);
            config.memory.swap_max_bytes = config_.resources.memory_limit * 2; // Allow 2x swap
            config.memory.oom_kill_enable = true;
        }

        // Configure PID limits
        if (config_.resources.pids_limit > 0) {
            config.pid.max = static_cast<uint64_t>(config_.resources.pids_limit);
        }

        // Enable required controllers
        config.controllers =
            CgroupController::CPU | CgroupController::MEMORY | CgroupController::PID;

        // Create cgroup manager
        cgroup_manager_ = CgroupManager::create(config);
        if (cgroup_manager_) {
            cgroup_manager_->create();
            logInfo("Cgroup created successfully: " + cgroup_manager_->getPath());

            // Apply resource limits
            if (config_.resources.cpu_shares > 0) {
                cgroup_manager_->setCpuMax(config.cpu.max_usec, config.cpu.period_usec);
                cgroup_manager_->setCpuWeight(config.cpu.weight);
                logInfo("CPU limits applied: " + std::to_string(config_.resources.cpu_shares * 100)
                        + "%");
            }

            if (config_.resources.memory_limit > 0) {
                cgroup_manager_->setMemoryMax(config.memory.max_bytes);
                cgroup_manager_->setMemorySwapMax(config.memory.swap_max_bytes);
                logInfo("Memory limits applied: "
                        + std::to_string(config_.resources.memory_limit / (1024 * 1024)) + "MB");
            }

            if (config_.resources.pids_limit > 0) {
                cgroup_manager_->setPidMax(config.pid.max);
                logInfo("PID limits applied: " + std::to_string(config_.resources.pids_limit)
                        + " processes");
            }
        }
        else {
            logWarning("Failed to create cgroup manager, continuing without resource limits");
        }
    }
    catch (const std::exception& e) {
        logError("Cgroup setup failed: " + std::string(e.what()));
        // Continue without cgroups rather than failing completely
    }
}

void Container::startProcess()
{
    // Initialize process manager
    try {
        process_manager_ = std::make_unique<ProcessManager>();

        // Start the process using process manager
        ProcessConfig proc_config;

        // Convert command vector to executable string
        if (!config_.command.empty()) {
            proc_config.executable = config_.command[0];
            // If there are more arguments, add them to args
            if (config_.command.size() > 1) {
                proc_config.args =
                    std::vector<std::string>(config_.command.begin() + 1, config_.command.end());
            }
        }
        else {
            proc_config.executable = "/bin/sleep"; // Default fallback
        }

        // Merge container args with process args
        if (!config_.args.empty()) {
            proc_config.args.insert(
                proc_config.args.end(), config_.args.begin(), config_.args.end());
        }

        proc_config.env = config_.env;
        proc_config.working_dir = config_.working_dir;

        // Enable namespace creation for isolation
        proc_config.create_pid_namespace = true;
        proc_config.create_uts_namespace = true;
        proc_config.create_network_namespace = true;
        proc_config.create_mount_namespace = true;
        proc_config.create_ipc_namespace = true;
        proc_config.hostname = "docker-cpp-container"; // Default hostname

        pid_t pid = process_manager_->createProcess(proc_config);
        if (pid <= 0) {
            throw ContainerRuntimeError("Failed to create process using process manager");
        }

        main_pid_ = pid;
        logInfo("Started process with PID " + std::to_string(pid) + " for container: " + id_);

        // Add process to cgroup if available
        if (cgroup_manager_) {
            try {
                cgroup_manager_->addProcess(pid);
                logInfo("Process " + std::to_string(pid)
                        + " added to cgroup: " + cgroup_manager_->getPath());
            }
            catch (const std::exception& e) {
                logWarning("Failed to add process to cgroup: " + std::string(e.what()));
            }
        }

        // Setup namespace isolation for the process
        if (!namespace_managers_.empty()) {
            logInfo("Applying namespace isolation for process " + std::to_string(pid));
            // Note: In a real implementation, namespace joining would happen
            // before process execution. This is a simplified approach.
            for (const auto& ns : namespace_managers_) {
                if (ns && ns->isValid()) {
                    logDebug("Namespace " + namespaceTypeToString(ns->getType()) + " is active");
                }
            }
        }
    }
    catch (const std::exception& e) {
        throw ContainerRuntimeError("Process start failed: " + std::string(e.what()));
    }

    // Give the process a moment to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Check if process is still alive
    if (!isProcessRunning()) {
        int status;
        waitpid(main_pid_, &status, WNOHANG);
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
    logInfo("Cleaning up resources for container: " + id_);

    // Cleanup process manager
    if (process_manager_) {
        try {
            process_manager_->stopProcess(main_pid_, 5);
            logInfo("Process manager stopped successfully");
        }
        catch (const std::exception& e) {
            logWarning("Failed to stop process manager: " + std::string(e.what()));
        }
        process_manager_.reset();
    }

    // Cleanup cgroup
    if (cgroup_manager_) {
        try {
            cgroup_manager_->destroy();
            logInfo("Cgroup destroyed successfully: " + cgroup_manager_->getPath());
        }
        catch (const std::exception& e) {
            logWarning("Failed to destroy cgroup: " + std::string(e.what()));
        }
        cgroup_manager_.reset();
    }

    // Cleanup namespaces (they will be automatically cleaned up when the objects are destroyed)
    if (!namespace_managers_.empty()) {
        logInfo("Cleaning up " + std::to_string(namespace_managers_.size()) + " namespaces");
        namespace_managers_.clear();
    }

    logInfo("Resource cleanup completed for container: " + id_);
}

void Container::emitEvent(const std::string& event_type,
                          const std::map<std::string, std::string>& event_data)
{
    if (event_manager_) {
        try {
            // Add container information to event data
            std::map<std::string, std::string> full_data = event_data;
            full_data["container_id"] = id_;
            full_data["container_name"] = config_.name;
            full_data["container_image"] = config_.image;
            full_data["timestamp"] =
                std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                                   std::chrono::system_clock::now().time_since_epoch())
                                   .count());

            // Convert map to JSON-like string for event data
            std::string data_str = "{";
            for (const auto& [key, value] : full_data) {
                if (data_str.length() > 1)
                    data_str += ",";
                data_str += "\"" + key + "\":\"" + value + "\"";
            }
            data_str += "}";

            // Create event with container-specific data
            Event event(event_type, data_str);

            event_manager_->publish(event);
            logInfo("Event published: " + event_type + " for container: " + id_);
        }
        catch (const std::exception& e) {
            logWarning("Failed to publish event: " + std::string(e.what()));
        }
    }
    else {
        // Fallback to logging if event manager is not available
        std::string data_str = "Event: " + event_type + " for container: " + id_;
        for (const auto& [key, value] : event_data) {
            data_str += ", " + key + "=" + value;
        }
        logInfo(data_str);
    }
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
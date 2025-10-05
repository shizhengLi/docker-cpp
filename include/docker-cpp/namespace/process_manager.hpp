#pragma once

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <docker-cpp/core/error.hpp>
#include <docker-cpp/namespace/namespace_manager.hpp>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace docker_cpp {

/**
 * @brief Constants for process management
 */
constexpr int DEFAULT_PROCESS_TIMEOUT = 10;
constexpr std::chrono::milliseconds PROCESS_CLEANUP_TIMEOUT{100};
constexpr std::chrono::milliseconds MONITORING_INTERVAL{500};
constexpr int CHILD_EXIT_CODE = 127;

/**
 * @brief Process configuration for container execution
 */
struct ProcessConfig {
    std::string executable;
    std::vector<std::string> args;
    std::vector<std::string> env;
    std::string working_dir;
    bool create_pid_namespace = false;
    bool create_uts_namespace = false;
    bool create_network_namespace = false;
    bool create_mount_namespace = false;
    bool create_ipc_namespace = false;
    bool create_user_namespace = false;
    bool create_cgroup_namespace = false;
    std::string hostname; // For UTS namespace
    uid_t uid = 0;        // For user namespace
    gid_t gid = 0;        // For user namespace
};

/**
 * @brief Process status information
 */
enum class ProcessStatus : std::uint8_t { RUNNING, STOPPED, ZOMBIE, UNKNOWN };

/**
 * @brief Process information structure
 */
struct ProcessInfo {
    pid_t pid;
    ProcessStatus status;
    int exit_code;
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point end_time;
    std::string command_line;
    bool has_pid_namespace;
    bool has_uts_namespace;
    bool has_network_namespace;
    bool has_mount_namespace;
    bool has_ipc_namespace;
    bool has_user_namespace;
    bool has_cgroup_namespace;
};

/**
 * @brief RAII process manager for container lifecycle
 *
 * This class provides safe process management with proper cleanup,
 * namespace handling, and resource management using RAII principles.
 */
class ProcessManager {
public:
    /**
     * @brief Process callback function type
     * @param info Process information
     */
    using ProcessCallback = std::function<void(const ProcessInfo&)>;

    /**
     * @brief Constructor
     */
    ProcessManager();

    /**
     * @brief Destructor - cleans up all managed processes
     */
    ~ProcessManager();

    /**
     * @brief Create a new process with namespace isolation
     * @param config Process configuration
     * @return Process ID of the created process
     * @throws ContainerError if process creation fails
     */
    pid_t createProcess(const ProcessConfig& config);

    /**
     * @brief Monitor a process and update its status
     * @param pid Process ID to monitor
     * @throws ContainerError if monitoring fails
     */
    void monitorProcess(pid_t pid);

    /**
     * @brief Stop a process gracefully
     * @param pid Process ID to stop
     * @param timeout Timeout in seconds before force kill
     * @return true if process was stopped, false if timeout occurred
     * @throws ContainerError if process stopping fails
     */
    bool stopProcess(pid_t pid, int timeout = DEFAULT_PROCESS_TIMEOUT);

    /**
     * @brief Force kill a process
     * @param pid Process ID to kill
     * @param signal Signal to send (default: SIGTERM)
     * @throws ContainerError if kill fails
     */
    void killProcess(pid_t pid, int signal = SIGTERM);

    /**
     * @brief Get process information
     * @param pid Process ID
     * @return Process information
     * @throws ContainerError if process not found
     */
    ProcessInfo getProcessInfo(pid_t pid) const;

    /**
     * @brief Get process status
     * @param pid Process ID
     * @return Process status
     * @throws ContainerError if process not found
     */
    ProcessStatus getProcessStatus(pid_t pid) const;

    /**
     * @brief Check if process is running
     * @param pid Process ID
     * @return true if process is running, false otherwise
     */
    bool isProcessRunning(pid_t pid) const;

    /**
     * @brief Wait for process to complete
     * @param pid Process ID to wait for
     * @param timeout Timeout in seconds (0 = wait forever)
     * @return true if process completed, false if timeout occurred
     * @throws ContainerError if wait fails
     */
    bool waitForProcess(pid_t pid, int timeout = 0);

    /**
     * @brief Get all managed processes
     * @return Vector of process IDs
     */
    std::vector<pid_t> getManagedProcesses() const;

    /**
     * @brief Set process exit callback
     * @param callback Function to call when process exits
     */
    void setProcessExitCallback(ProcessCallback callback);

    /**
     * @brief Start background monitoring thread
     */
    void startMonitoring();

    /**
     * @brief Stop background monitoring thread
     */
    void stopMonitoring();

    /**
     * @brief Check if monitoring is active
     * @return true if monitoring is active
     */
    bool isMonitoringActive() const;

    // Non-copyable but movable
    ProcessManager(const ProcessManager&) = delete;
    ProcessManager& operator=(const ProcessManager&) = delete;
    ProcessManager(ProcessManager&& other) noexcept;
    ProcessManager& operator=(ProcessManager&& other) noexcept;

private:
    mutable std::mutex processes_mutex_;
    std::unordered_map<pid_t, ProcessInfo> managed_processes_;
    ProcessCallback exit_callback_;
    std::unique_ptr<std::thread> monitoring_thread_;
    std::atomic<bool> should_stop_monitoring_;
    std::atomic<bool> monitoring_active_;

    void updateProcessStatus(pid_t pid);
    void monitoringLoop();
    void cleanupProcess(pid_t pid);
    void setupChildProcess(const ProcessConfig& config);
    void setupNamespaces(const ProcessConfig& config);
    std::vector<char*> createArgv(const std::vector<std::string>& args);
    std::vector<char*> createEnvp(const std::vector<std::string>& env);
    void freeArgv(std::vector<char*>& argv);
    void freeEnvp(std::vector<char*>& envp);
    ProcessStatus determineProcessStatus(pid_t pid) const;
};

} // namespace docker_cpp
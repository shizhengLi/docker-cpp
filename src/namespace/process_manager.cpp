#include <docker-cpp/namespace/process_manager.hpp>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <thread>
#include <unistd.h>
#include <fcntl.h>

namespace docker_cpp {

ProcessManager::ProcessManager()
    : should_stop_monitoring_(false), monitoring_active_(false)
{
}

ProcessManager::~ProcessManager()
{
    stopMonitoring();

    // Clean up all managed processes
    std::lock_guard<std::mutex> lock(processes_mutex_);
    for (auto& [pid, info] : managed_processes_) {
        if (isProcessRunning(pid)) {
            kill(pid, SIGTERM);
            // Give process a chance to exit gracefully
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (isProcessRunning(pid)) {
                kill(pid, SIGKILL);
            }
        }
    }
    managed_processes_.clear();
}

pid_t ProcessManager::createProcess(const ProcessConfig& config)
{
    // Create a pipe for error communication
    int error_pipe[2];
    if (pipe(error_pipe) == -1) {
        throw ContainerError(ErrorCode::PROCESS_CREATION_FAILED,
                             "Failed to create error pipe: " + std::string(strerror(errno)));
    }

    pid_t pid = fork();

    if (pid == -1) {
        close(error_pipe[0]);
        close(error_pipe[1]);
        throw ContainerError(ErrorCode::PROCESS_CREATION_FAILED,
                             "Failed to fork process: " + std::string(strerror(errno)));
    }

    if (pid == 0) {
        // Child process
        close(error_pipe[0]); // Close read end

        try {
            setupChildProcess(config);

            // Execute the program
            std::vector<char*> argv = createArgv(config.args);
            std::vector<char*> envp = createEnvp(config.env);

            if (!config.working_dir.empty()) {
                if (chdir(config.working_dir.c_str()) == -1) {
                    int error_code = errno;
                    write(error_pipe[1], &error_code, sizeof(error_code));
                    close(error_pipe[1]);
                    exit(127);
                }
            }

            execve(config.executable.c_str(), argv.data(), envp.data());

            // If we reach here, execve failed - send error to parent
            int error_code = errno;
            write(error_pipe[1], &error_code, sizeof(error_code));
            close(error_pipe[1]);

            freeArgv(argv);
            freeEnvp(envp);
            exit(127);
        }
        catch (...) {
            int error_code = ECANCELED;
            write(error_pipe[1], &error_code, sizeof(error_code));
            close(error_pipe[1]);
            exit(127);
        }
    } else {
        // Parent process
        close(error_pipe[1]); // Close write end

        // Check for errors from child with non-blocking read
        int flags = fcntl(error_pipe[0], F_GETFL);
        fcntl(error_pipe[0], F_SETFL, flags | O_NONBLOCK);

        int child_error = 0;
        ssize_t bytes_read = read(error_pipe[0], &child_error, sizeof(child_error));
        close(error_pipe[0]);

        if (bytes_read == sizeof(child_error)) {
            // Child reported an error
            int status;
            waitpid(pid, &status, 0); // Clean up zombie
            throw ContainerError(ErrorCode::PROCESS_CREATION_FAILED,
                                 "Failed to execute '" + config.executable + "': " + std::string(strerror(child_error)));
        } else if (bytes_read == -1 && errno != EAGAIN) {
            // Unexpected error reading from pipe
            int status;
            waitpid(pid, &status, 0); // Clean up zombie
            throw ContainerError(ErrorCode::PROCESS_CREATION_FAILED,
                                 "Failed to read error status from child: " + std::string(strerror(errno)));
        }
        // If bytes_read is 0 (EOF) or -1 with EAGAIN, the child exec'd successfully

        // No error from child, process should be running
        ProcessInfo info;
        info.pid = pid;
        info.status = ProcessStatus::RUNNING;
        info.exit_code = 0;
        info.start_time = std::chrono::system_clock::now();
        info.command_line = config.executable;
        for (const auto& arg : config.args) {
            info.command_line += " " + arg;
        }

        // Track namespace creation
        info.has_pid_namespace = config.create_pid_namespace;
        info.has_uts_namespace = config.create_uts_namespace;
        info.has_network_namespace = config.create_network_namespace;
        info.has_mount_namespace = config.create_mount_namespace;
        info.has_ipc_namespace = config.create_ipc_namespace;
        info.has_user_namespace = config.create_user_namespace;
        info.has_cgroup_namespace = config.create_cgroup_namespace;

        {
            std::lock_guard<std::mutex> lock(processes_mutex_);
            managed_processes_[pid] = info;
        }

        return pid;
    }
}

void ProcessManager::monitorProcess(pid_t pid)
{
    updateProcessStatus(pid);
}

bool ProcessManager::stopProcess(pid_t pid, int timeout)
{
    // Check if process is managed
    {
        std::lock_guard<std::mutex> lock(processes_mutex_);
        if (managed_processes_.find(pid) == managed_processes_.end()) {
            throw ContainerError(ErrorCode::PROCESS_NOT_FOUND,
                                 "Process " + std::to_string(pid) + " not found in managed processes");
        }
    }

    if (!isProcessRunning(pid)) {
        return true;
    }

    // Send SIGTERM first
    if (kill(pid, SIGTERM) == -1) {
        throw ContainerError(ErrorCode::PROCESS_STOP_FAILED,
                             "Failed to send SIGTERM to process " + std::to_string(pid));
    }

    // Wait for process to exit
    auto start_time = std::chrono::steady_clock::now();
    while (isProcessRunning(pid)) {
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        if (elapsed >= std::chrono::seconds(timeout)) {
            // Timeout reached, force kill
            killProcess(pid, SIGKILL);
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    updateProcessStatus(pid);
    return true;
}

void ProcessManager::killProcess(pid_t pid, int signal)
{
    // Check if process is managed
    {
        std::lock_guard<std::mutex> lock(processes_mutex_);
        if (managed_processes_.find(pid) == managed_processes_.end()) {
            throw ContainerError(ErrorCode::PROCESS_NOT_FOUND,
                                 "Process " + std::to_string(pid) + " not found in managed processes");
        }
    }

    if (kill(pid, signal) == -1) {
        throw ContainerError(ErrorCode::PROCESS_STOP_FAILED,
                             "Failed to send signal to process " + std::to_string(pid));
    }
    updateProcessStatus(pid);
}

ProcessInfo ProcessManager::getProcessInfo(pid_t pid) const
{
    std::lock_guard<std::mutex> lock(processes_mutex_);
    auto it = managed_processes_.find(pid);
    if (it == managed_processes_.end()) {
        throw ContainerError(ErrorCode::PROCESS_NOT_FOUND,
                             "Process " + std::to_string(pid) + " not found");
    }

    // Update status before returning
    ProcessInfo info = it->second;
    info.status = determineProcessStatus(pid);
    return info;
}

ProcessStatus ProcessManager::getProcessStatus(pid_t pid) const
{
    return determineProcessStatus(pid);
}

bool ProcessManager::isProcessRunning(pid_t pid) const
{
    return determineProcessStatus(pid) == ProcessStatus::RUNNING;
}

bool ProcessManager::waitForProcess(pid_t pid, int timeout)
{
    auto start_time = std::chrono::steady_clock::now();

    while (isProcessRunning(pid)) {
        if (timeout > 0) {
            auto elapsed = std::chrono::steady_clock::now() - start_time;
            if (elapsed >= std::chrono::seconds(timeout)) {
                return false;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    updateProcessStatus(pid);
    return true;
}

std::vector<pid_t> ProcessManager::getManagedProcesses() const
{
    std::lock_guard<std::mutex> lock(processes_mutex_);
    std::vector<pid_t> pids;
    pids.reserve(managed_processes_.size());

    for (const auto& [pid, info] : managed_processes_) {
        pids.push_back(pid);
    }

    return pids;
}

void ProcessManager::setProcessExitCallback(ProcessCallback callback)
{
    exit_callback_ = std::move(callback);
}

void ProcessManager::startMonitoring()
{
    if (monitoring_active_) {
        return;
    }

    should_stop_monitoring_ = false;
    monitoring_thread_ = std::make_unique<std::thread>(&ProcessManager::monitoringLoop, this);
    monitoring_active_ = true;
}

void ProcessManager::stopMonitoring()
{
    if (!monitoring_active_) {
        return;
    }

    should_stop_monitoring_ = true;
    if (monitoring_thread_ && monitoring_thread_->joinable()) {
        monitoring_thread_->join();
    }
    monitoring_thread_.reset();
    monitoring_active_ = false;
}

bool ProcessManager::isMonitoringActive() const
{
    return monitoring_active_;
}

ProcessManager::ProcessManager(ProcessManager&& other) noexcept
    : managed_processes_(std::move(other.managed_processes_)),
      exit_callback_(std::move(other.exit_callback_)),
      monitoring_thread_(std::move(other.monitoring_thread_)),
      should_stop_monitoring_(other.should_stop_monitoring_.load()),
      monitoring_active_(other.monitoring_active_.load())
{
    other.should_stop_monitoring_ = true;
    other.monitoring_active_ = false;
}

ProcessManager& ProcessManager::operator=(ProcessManager&& other) noexcept
{
    if (this != &other) {
        stopMonitoring();

        managed_processes_ = std::move(other.managed_processes_);
        exit_callback_ = std::move(other.exit_callback_);
        monitoring_thread_ = std::move(other.monitoring_thread_);
        should_stop_monitoring_ = other.should_stop_monitoring_.load();
        monitoring_active_ = other.monitoring_active_.load();

        other.should_stop_monitoring_ = true;
        other.monitoring_active_ = false;
    }
    return *this;
}

void ProcessManager::updateProcessStatus(pid_t pid)
{
    ProcessStatus new_status = determineProcessStatus(pid);

    std::lock_guard<std::mutex> lock(processes_mutex_);
    auto it = managed_processes_.find(pid);
    if (it != managed_processes_.end()) {
        ProcessStatus old_status = it->second.status;
        it->second.status = new_status;

        // If process just exited, record end time and call callback
        if (old_status == ProcessStatus::RUNNING && new_status != ProcessStatus::RUNNING) {
            it->second.end_time = std::chrono::system_clock::now();

            // Get exit code if process is a zombie
            int status;
            if (waitpid(pid, &status, WNOHANG) == pid) {
                if (WIFEXITED(status)) {
                    it->second.exit_code = WEXITSTATUS(status);
                } else if (WIFSIGNALED(status)) {
                    it->second.exit_code = -WTERMSIG(status);
                }
            }

            if (exit_callback_) {
                exit_callback_(it->second);
            }
        }
    }
}

void ProcessManager::monitoringLoop()
{
    while (!should_stop_monitoring_) {
        std::vector<pid_t> pids;
        {
            std::lock_guard<std::mutex> lock(processes_mutex_);
            pids.reserve(managed_processes_.size());
            for (const auto& [pid, info] : managed_processes_) {
                pids.push_back(pid);
            }
        }

        for (pid_t pid : pids) {
            updateProcessStatus(pid);
        }

        // Clean up zombie processes
        {
            std::lock_guard<std::mutex> lock(processes_mutex_);
            auto it = managed_processes_.begin();
            while (it != managed_processes_.end()) {
                if (it->second.status == ProcessStatus::STOPPED ||
                    it->second.status == ProcessStatus::ZOMBIE) {
                    it = managed_processes_.erase(it);
                } else {
                    ++it;
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void ProcessManager::cleanupProcess(pid_t pid)
{
    std::lock_guard<std::mutex> lock(processes_mutex_);
    managed_processes_.erase(pid);
}

void ProcessManager::setupChildProcess(const ProcessConfig& config)
{
    setupNamespaces(config);

    // Set up signal handlers
    signal(SIGTERM, SIG_DFL);
    signal(SIGINT, SIG_DFL);
    signal(SIGHUP, SIG_DFL);
}

void ProcessManager::setupNamespaces(const ProcessConfig& config)
{
    // Create namespaces based on configuration
    std::vector<std::unique_ptr<NamespaceManager>> namespaces;

    try {
        if (config.create_pid_namespace) {
            namespaces.push_back(std::make_unique<NamespaceManager>(NamespaceType::PID));
        }
        if (config.create_uts_namespace) {
            namespaces.push_back(std::make_unique<NamespaceManager>(NamespaceType::UTS));
            // Set hostname if provided
            if (!config.hostname.empty()) {
                // In a real implementation, we would set the hostname here
                // For now, we just acknowledge the hostname was provided
            }
        }
        if (config.create_network_namespace) {
            namespaces.push_back(std::make_unique<NamespaceManager>(NamespaceType::NETWORK));
        }
        if (config.create_mount_namespace) {
            namespaces.push_back(std::make_unique<NamespaceManager>(NamespaceType::MOUNT));
        }
        if (config.create_ipc_namespace) {
            namespaces.push_back(std::make_unique<NamespaceManager>(NamespaceType::IPC));
        }
        if (config.create_user_namespace) {
            namespaces.push_back(std::make_unique<NamespaceManager>(NamespaceType::USER));
            // In a real implementation, we would set up UID/GID mapping here
        }
        if (config.create_cgroup_namespace) {
            namespaces.push_back(std::make_unique<NamespaceManager>(NamespaceType::CGROUP));
        }

        // Namespaces will be automatically cleaned up when they go out of scope
        // or when the process exits

    } catch (const ContainerError& e) {
        exit(127);
    }
}

std::vector<char*> ProcessManager::createArgv(const std::vector<std::string>& args)
{
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);

    for (const auto& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    return argv;
}

std::vector<char*> ProcessManager::createEnvp(const std::vector<std::string>& env)
{
    std::vector<char*> envp;
    envp.reserve(env.size() + 1);

    for (const auto& e : env) {
        envp.push_back(const_cast<char*>(e.c_str()));
    }
    envp.push_back(nullptr);

    return envp;
}

void ProcessManager::freeArgv(std::vector<char*>& argv)
{
    // Arguments are owned by the string vectors, no need to free individual elements
    argv.clear();
}

void ProcessManager::freeEnvp(std::vector<char*>& envp)
{
    // Environment variables are owned by the string vectors, no need to free individual elements
    envp.clear();
}

ProcessStatus ProcessManager::determineProcessStatus(pid_t pid) const
{
    // Check if process exists by sending signal 0
    if (kill(pid, 0) == -1) {
        if (errno == ESRCH) {
            return ProcessStatus::UNKNOWN; // Process doesn't exist
        }
        return ProcessStatus::UNKNOWN;
    }

    // Check if process is a zombie
    int status;
    pid_t result = waitpid(pid, &status, WNOHANG);
    if (result == pid) {
        return ProcessStatus::ZOMBIE;
    } else if (result == -1) {
        return ProcessStatus::UNKNOWN;
    }

    // Process is running
    return ProcessStatus::RUNNING;
}

} // namespace docker_cpp
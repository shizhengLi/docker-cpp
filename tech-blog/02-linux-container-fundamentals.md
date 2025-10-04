# Linux Container Fundamentals: Namespaces, Cgroups, and Beyond

## Introduction

Container technology relies on fundamental Linux kernel features that provide process isolation and resource control. Understanding these primitives is essential for implementing a container runtime from scratch. This article explores the core mechanisms that make containers possible, with practical C++ implementations and real-world examples.

## Linux Namespaces: The Foundation of Isolation

Namespaces are the primary mechanism for container isolation. They create separate views of system resources, making processes believe they're running in their own isolated environment.

### Available Namespace Types

| Namespace | Isolates | Kernel Version | C++ Constant |
|-----------|----------|----------------|--------------|
| PID | Process IDs | 2.6.24 | CLONE_NEWPID |
| Network | Network devices, IPs, routing | 2.6.29 | CLONE_NEWNET |
| Mount | Filesystem mount points | 2.4.19 | CLONE_NEWNS |
| UTS | Hostname and domain name | 2.6.19 | CLONE_NEWUTS |
| IPC | System V IPC, POSIX message queues | 2.6.19 | CLONE_NEWIPC |
| User | User and group IDs | 3.8 | CLONE_NEWUSER |
| Cgroup | Cgroup root directory | 4.6 | CLONE_NEWCGROUP |

### C++ Namespace Wrapper Implementation

```cpp
#include <sys/types.h>
#include <sys/wait.h>
#include <sched.h>
#include <unistd.h>
#include <system_error>
#include <string>
#include <memory>

class NamespaceManager {
public:
    enum class Type {
        PID = CLONE_NEWPID,
        NETWORK = CLONE_NEWNET,
        MOUNT = CLONE_NEWNS,
        UTS = CLONE_NEWUTS,
        IPC = CLONE_NEWIPC,
        USER = CLONE_NEWUSER,
        CGROUP = CLONE_NEWCGROUP
    };

    explicit NamespaceManager(Type type) : type_(type), fd_(-1) {
        fd_ = ::unshare(static_cast<int>(type_));
        if (fd_ == -1) {
            throw std::system_error(errno, std::system_category(),
                                  "Failed to create namespace");
        }
    }

    ~NamespaceManager() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    // Non-copyable, movable
    NamespaceManager(const NamespaceManager&) = delete;
    NamespaceManager& operator=(const NamespaceManager&) = delete;
    NamespaceManager(NamespaceManager&& other) noexcept
        : type_(other.type_), fd_(other.fd_) {
        other.fd_ = -1;
    }

    static void join(pid_t pid, Type type) {
        std::string ns_path = "/proc/" + std::to_string(pid) + "/ns/" +
                             namespaceTypeToString(type);
        int fd = ::open(ns_path.c_str(), O_RDONLY);
        if (fd == -1) {
            throw std::system_error(errno, std::system_category(),
                                  "Failed to open namespace file");
        }

        if (::setns(fd, static_cast<int>(type_)) == -1) {
            ::close(fd);
            throw std::system_error(errno, std::system_category(),
                                  "Failed to join namespace");
        }
        ::close(fd);
    }

private:
    Type type_;
    int fd_;

    static std::string namespaceTypeToString(Type type) {
        switch (type) {
            case Type::PID: return "pid";
            case Type::NETWORK: return "net";
            case Type::MOUNT: return "mnt";
            case Type::UTS: return "uts";
            case Type::IPC: return "ipc";
            case Type::USER: return "user";
            case Type::CGROUP: return "cgroup";
            default: return "";
        }
    }
};
```

### PID Namespace Deep Dive

PID namespaces isolate process IDs, allowing each container to have its own process tree starting from PID 1.

```cpp
class PidNamespace : public NamespaceManager {
public:
    PidNamespace() : NamespaceManager(Type::PID) {}

    pid_t spawnInNamespace(const std::string& command,
                          const std::vector<std::string>& args) {
        pid_t pid = ::fork();
        if (pid == -1) {
            throw std::system_error(errno, std::system_category(),
                                  "Failed to fork process");
        }

        if (pid == 0) {
            // Child process - we're already in the new PID namespace

            // Mount /proc for the new namespace
            if (::mount("proc", "/proc", "proc", 0, nullptr) == -1) {
                std::cerr << "Failed to mount /proc: " << strerror(errno) << std::endl;
                _exit(1);
            }

            // Prepare argv
            std::vector<char*> argv;
            argv.push_back(const_cast<char*>(command.c_str()));
            for (const auto& arg : args) {
                argv.push_back(const_cast<char*>(arg.c_str()));
            }
            argv.push_back(nullptr);

            // Execute the command
            ::execvp(command.c_str(), argv.data());

            // If we reach here, exec failed
            std::cerr << "Failed to execute command: " << strerror(errno) << std::endl;
            _exit(127);
        }

        return pid; // Parent process returns child PID
    }

    void waitPid(pid_t pid, int* status = nullptr) {
        ::waitpid(pid, status, 0);
    }
};
```

### Network Namespace Management

Network namespaces provide complete network isolation, allowing containers to have their own network interfaces, IP addresses, and routing tables.

```cpp
#include <net/if.h>
#include <linux/if_tun.h>
#include <sys/ioctl.h>

class NetworkNamespace : public NamespaceManager {
public:
    NetworkNamespace() : NamespaceManager(Type::NETWORK) {}

    void createBridge(const std::string& bridge_name) {
        // Create bridge interface
        int sock = ::socket(AF_INET, SOCK_STREAM, 0);
        if (sock == -1) {
            throw std::system_error(errno, std::system_category(),
                                  "Failed to create socket");
        }

        struct ifreq ifr;
        memset(&ifr, 0, sizeof(ifr));
        strncpy(ifr.ifr_name, bridge_name.c_str(), IFNAMSIZ - 1);

        // Create bridge
        if (::ioctl(sock, SIOCBRADDBR, &ifr) == -1) {
            ::close(sock);
            throw std::system_error(errno, std::system_category(),
                                  "Failed to create bridge");
        }

        // Bring bridge up
        ifr.ifr_flags |= IFF_UP;
        if (::ioctl(sock, SIOCSIFFLAGS, &ifr) == -1) {
            ::close(sock);
            throw std::system_error(errno, std::system_category(),
                                  "Failed to bring bridge up");
        }

        ::close(sock);
    }

    void connectToBridge(const std::string& interface_name,
                        const std::string& bridge_name) {
        // Implementation for connecting interface to bridge
        // This involves creating veth pairs and configuration
    }

    void assignIpAddress(const std::string& interface_name,
                        const std::string& ip_address,
                        const std::string& netmask) {
        // IP address assignment implementation
    }
};
```

## Control Groups: Resource Management

Control Groups (cgroups) limit, account for, and isolate resource usage of process groups.

### Cgroup Hierarchy and Controllers

```cpp
#include <sys/stat.h>
#include <fcntl.h>
#include <fstream>

class CgroupManager {
public:
    enum class Controller {
        CPU = 0x01,
        MEMORY = 0x02,
        BLKIO = 0x04,
        CPUSET = 0x08,
        FREEZER = 0x10,
        DEVICES = 0x20
    };

    explicit CgroupManager(const std::string& group_name,
                          Controller controllers = Controller::CPU | Controller::MEMORY)
        : group_name_(group_name), controllers_(controllers) {
        createCgroup();
    }

    ~CgroupManager() {
        try {
            destroyCgroup();
        } catch (...) {
            // Log error but don't throw in destructor
        }
    }

    void addProcess(pid_t pid) {
        for (auto controller : getEnabledControllers()) {
            std::string tasks_path = getCgroupPath(controller) + "/tasks";
            std::ofstream tasks_file(tasks_path);
            if (!tasks_file) {
                throw std::runtime_error("Failed to open tasks file for " +
                                       controllerToString(controller));
            }
            tasks_file << pid;
        }
    }

    void setCpuLimit(double cpu_shares) {
        if (!(controllers_ & Controller::CPU)) {
            throw std::runtime_error("CPU controller not enabled");
        }

        std::string cpu_shares_path = getCgroupPath(Controller::CPU) + "/cpu.shares";
        std::ofstream cpu_file(cpu_shares_path);
        if (!cpu_file) {
            throw std::runtime_error("Failed to open cpu.shares file");
        }

        int shares = static_cast<int>(cpu_shares * 1024); // Default is 1024
        cpu_file << shares;
    }

    void setMemoryLimit(size_t memory_bytes) {
        if (!(controllers_ & Controller::MEMORY)) {
            throw std::runtime_error("Memory controller not enabled");
        }

        std::string memory_limit_path = getCgroupPath(Controller::MEMORY) + "/memory.limit_in_bytes";
        std::ofstream memory_file(memory_limit_path);
        if (!memory_file) {
            throw std::runtime_error("Failed to open memory.limit_in_bytes file");
        }

        memory_file << memory_bytes;
    }

    std::vector<size_t> getCpuUsage() {
        if (!(controllers_ & Controller::CPU)) {
            throw std::runtime_error("CPU controller not enabled");
        }

        std::string cpuacct_path = getCgroupPath(Controller::CPU) + "/cpuacct.usage";
        std::ifstream cpuacct_file(cpuacct_path);
        if (!cpuacct_file) {
            throw std::runtime_error("Failed to open cpuacct.usage file");
        }

        size_t usage;
        cpuacct_file >> usage;
        return {usage}; // Return vector for future multi-core support
    }

private:
    std::string group_name_;
    Controller controllers_;

    void createCgroup() {
        for (auto controller : getEnabledControllers()) {
            std::string controller_path = "/sys/fs/cgroup/" +
                                        controllerToString(controller) + "/" + group_name_;

            if (::mkdir(controller_path.c_str(), 0755) == -1 && errno != EEXIST) {
                throw std::system_error(errno, std::system_category(),
                                      "Failed to create cgroup directory");
            }
        }
    }

    void destroyCgroup() {
        for (auto controller : getEnabledControllers()) {
            std::string controller_path = "/sys/fs/cgroup/" +
                                        controllerToString(controller) + "/" + group_name_;

            // Remove all processes first
            std::string tasks_path = controller_path + "/tasks";
            std::ifstream tasks_file(tasks_path);
            if (tasks_file) {
                pid_t pid;
                while (tasks_file >> pid) {
                    // Move processes to parent cgroup
                    std::string parent_tasks = "/sys/fs/cgroup/" +
                                             controllerToString(controller) + "/tasks";
                    std::ofstream parent_file(parent_tasks);
                    if (parent_file) {
                        parent_file << pid;
                    }
                }
            }

            // Remove the cgroup directory
            ::rmdir(controller_path.c_str());
        }
    }

    std::string getCgroupPath(Controller controller) const {
        return "/sys/fs/cgroup/" + controllerToString(controller) + "/" + group_name_;
    }

    std::vector<Controller> getEnabledControllers() const {
        std::vector<Controller> controllers;

        if (controllers_ & Controller::CPU) controllers.push_back(Controller::CPU);
        if (controllers_ & Controller::MEMORY) controllers.push_back(Controller::MEMORY);
        if (controllers_ & Controller::BLKIO) controllers.push_back(Controller::BLKIO);
        if (controllers_ & Controller::CPUSET) controllers.push_back(Controller::CPUSET);
        if (controllers_ & Controller::FREEZER) controllers.push_back(Controller::FREEZER);
        if (controllers_ & Controller::DEVICES) controllers.push_back(Controller::DEVICES);

        return controllers;
    }

    static std::string controllerToString(Controller controller) {
        switch (controller) {
            case Controller::CPU: return "cpu,cpuacct";
            case Controller::MEMORY: return "memory";
            case Controller::BLKIO: return "blkio";
            case Controller::CPUSET: return "cpuset";
            case Controller::FREEZER: return "freezer";
            case Controller::DEVICES: return "devices";
            default: return "";
        }
    }
};
```

## Practical Container Creation Example

Let's combine namespaces and cgroups to create a basic container:

```cpp
#include <iostream>
#include <memory>
#include <chrono>

class BasicContainer {
public:
    explicit BasicContainer(const std::string& name)
        : name_(name), pid_(-1) {}

    void start(const std::string& command,
              const std::vector<std::string>& args,
              double cpu_limit = 1.0,
              size_t memory_limit = 1024 * 1024 * 1024) { // 1GB default

        // Create container-specific cgroup
        cgroup_ = std::make_unique<CgroupManager>("container_" + name_);
        cgroup_->setCpuLimit(cpu_limit);
        cgroup_->setMemoryLimit(memory_limit);

        // Fork process
        pid_ = ::fork();
        if (pid_ == -1) {
            throw std::system_error(errno, std::system_category(),
                                  "Failed to fork container process");
        }

        if (pid_ == 0) {
            // Child process - setup namespaces and run command
            setupChildProcess(command, args);
        } else {
            // Parent process - add child to cgroup
            cgroup_->addProcess(pid_);
            std::cout << "Container " << name_ << " started with PID: " << pid_ << std::endl;
        }
    }

    void stop() {
        if (pid_ > 0) {
            ::kill(pid_, SIGTERM);

            // Wait for graceful shutdown
            int status;
            int wait_result = ::waitpid(pid_, &status, WNOHANG);
            int timeout = 10; // 10 seconds

            while (wait_result == 0 && timeout > 0) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                wait_result = ::waitpid(pid_, &status, WNOHANG);
                timeout--;
            }

            if (wait_result == 0) {
                // Force kill if still running
                ::kill(pid_, SIGKILL);
                ::waitpid(pid_, &status, 0);
            }

            pid_ = -1;
            std::cout << "Container " << name_ << " stopped" << std::endl;
        }
    }

    pid_t getPid() const { return pid_; }

    ~BasicContainer() {
        if (pid_ > 0) {
            stop();
        }
    }

private:
    std::string name_;
    pid_t pid_;
    std::unique_ptr<CgroupManager> cgroup_;

    void setupChildProcess(const std::string& command,
                          const std::vector<std::string>& args) {
        try {
            // Setup namespaces
            auto pid_ns = std::make_unique<PidNamespace>();
            auto net_ns = std::make_unique<NetworkNamespace>();
            auto mnt_ns = std::make_unique<NamespaceManager>(NamespaceManager::Type::MOUNT);
            auto uts_ns = std::make_unique<NamespaceManager>(NamespaceManager::Type::UTS);
            auto ipc_ns = std::make_unique<NamespaceManager>(NamespaceManager::Type::IPC);

            // Mount new proc filesystem
            if (::mount("proc", "/proc", "proc", 0, nullptr) == -1) {
                std::cerr << "Failed to mount /proc: " << strerror(errno) << std::endl;
                _exit(1);
            }

            // Setup hostname
            sethostname(name_.c_str(), name_.length());

            // Prepare command arguments
            std::vector<char*> argv;
            argv.push_back(const_cast<char*>(command.c_str()));
            for (const auto& arg : args) {
                argv.push_back(const_cast<char*>(arg.c_str()));
            }
            argv.push_back(nullptr);

            // Execute the command
            ::execvp(command.c_str(), argv.data());

            // If we reach here, exec failed
            std::cerr << "Failed to execute command: " << strerror(errno) << std::endl;
            _exit(127);
        } catch (const std::exception& e) {
            std::cerr << "Exception in child process: " << e.what() << std::endl;
            _exit(1);
        }
    }
};
```

## Usage Example

```cpp
int main() {
    try {
        BasicContainer container("test_container");

        // Start container with limited resources
        container.start("/bin/bash", {"-l", "-c", "echo 'Hello from container!' && sleep 5"},
                       0.5, // 50% CPU
                       512 * 1024 * 1024); // 512MB memory

        // Monitor container
        std::this_thread::sleep_for(std::chrono::seconds(2));

        // Stop container
        container.stop();

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
```

## Security Considerations

### 1. Namespace Security
- **User namespaces**: Map container root to unprivileged host user
- **Mount namespaces**: Read-only filesystems where possible
- **Network namespaces**: Restrict network access as needed

### 2. Cgroup Security
- **Device restrictions**: Control access to devices
- **Resource limits**: Prevent resource exhaustion attacks
- **Freezer controller**: For secure container manipulation

### 3. Secure Default Configuration
```cpp
class SecureContainerDefaults {
public:
    static void applySecureDefaults(BasicContainer& container) {
        // Drop unnecessary capabilities
        // Set read-only filesystems
        // Restrict system calls with seccomp
        // Enable AppArmor/SELinux profiles
    }
};
```

## Performance Considerations

### 1. Namespace Creation Overhead
- Namespace creation is relatively cheap (~1-2ms per namespace)
- Consider sharing namespaces for related containers
- Cache namespace file descriptors for reuse

### 2. Cgroup Management
- Cgroup operations involve filesystem I/O
- Batch operations when possible
- Use cgroup v2 for better performance

### 3. Memory Efficiency
- Use stack allocation for small, frequent operations
- Minimize dynamic memory allocation in hot paths
- Consider memory pools for container metadata

## Troubleshooting Common Issues

### Permission Denied Errors
```cpp
// Ensure proper permissions for cgroup operations
void checkCgroupPermissions() {
    if (::access("/sys/fs/cgroup", R_OK | W_OK) == -1) {
        throw std::runtime_error("Insufficient permissions for cgroup operations");
    }
}
```

### Namespace Join Failures
```cpp
// Handle namespace joining gracefully
bool tryJoinNamespace(pid_t pid, NamespaceManager::Type type) {
    try {
        NamespaceManager::join(pid, type);
        return true;
    } catch (const std::system_error& e) {
        if (e.code() == std::errc::no_such_file_or_directory) {
            std::cerr << "Process " << pid << " does not exist" << std::endl;
        } else if (e.code() == std::errc::permission_denied) {
            std::cerr << "Insufficient privileges to join namespace" << std::endl;
        }
        return false;
    }
}
```

## Conclusion

Understanding Linux namespaces and cgroups is fundamental to implementing a container runtime. This article has covered the essential concepts with practical C++ implementations that form the foundation of our docker-cpp project.

The combination of precise resource control and strong isolation mechanisms provides the security and efficiency needed for modern containerization. As we progress through this series, we'll build upon these fundamentals to create a complete container runtime system.

## Next Steps

In our next article, we'll explore "C++ Systems Programming for Container Runtimes," diving into modern C++ features that make it ideal for systems programming, error handling patterns, and performance optimization techniques for container implementations.

---

**Previous Article**: [Project Vision and Architecture](./01-project-vision-and-architecture.md)
**Next Article**: [C++ Systems Programming for Container Runtimes](./03-cpp-systems-programming.md)
**Series Index**: [Table of Contents](./00-table-of-contents.md)
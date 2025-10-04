# Process Isolation: Implementing Linux Namespaces in C++

## Introduction

Process isolation is the cornerstone of container technology, enabling multiple containers to run simultaneously on the same host without interfering with each other. This article explores the practical implementation of Linux namespaces in C++, providing deep insights into the mechanisms that make containers possible.

## Linux Namespace Types and Their Purpose

Linux namespaces provide isolation for various system resources. Each namespace type isolates a specific aspect of the system:

| Namespace Type | Constant | Resource Isolated | Use Case in Containers |
|----------------|----------|-------------------|-----------------------|
| PID | CLONE_NEWPID | Process IDs | Each container has its own PID 1 |
| Network | CLONE_NEWNET | Network devices, IPs, routing | Isolated network stacks |
| Mount | CLONE_NEWNS | Filesystem mount points | Separate filesystem views |
| UTS | CLONE_NEWUTS | Hostname and domain name | Container-specific hostnames |
| IPC | CLONE_NEWIPC | System V IPC, POSIX message queues | Isolated inter-process communication |
| User | CLONE_NEWUSER | User and group IDs | Root user mapping |
| Cgroup | CLONE_NEWCGROUP | Cgroup root directory | Independent resource control |

## Namespace Management Architecture

### 1. Namespace Manager Base Class

```cpp
#include <sys/types.h>
#include <sys/wait.h>
#include <sched.h>
#include <unistd.h>
#include <fcntl.h>
#include <system_error>
#include <string>
#include <memory>
#include <unordered_map>

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

    virtual ~NamespaceManager() = default;

    // Pure virtual methods for derived classes
    virtual void create() = 0;
    virtual void join(pid_t pid) = 0;
    virtual std::string getTypeName() const = 0;
    virtual Type getNamespaceType() const = 0;

    // Common utility methods
    static std::string getNamespacePath(pid_t pid, Type type);
    static bool namespaceExists(pid_t pid, Type type);
    static std::vector<std::string> listNamespaces(pid_t pid);

protected:
    std::string namespaceTypeToString(Type type) const;
    void validateNamespaceOperation(const std::string& operation) const;
};
```

### 2. PID Namespace Implementation

```cpp
class PidNamespace : public NamespaceManager {
public:
    PidNamespace() : fd_(-1), created_(false) {}

    ~PidNamespace() override {
        if (fd_ >= 0) {
            close(fd_);
        }
    }

    void create() override {
        if (created_) {
            throw std::runtime_error("PID namespace already created");
        }

        // Create new PID namespace by unsharing
        if (unshare(static_cast<int>(Type::PID)) == -1) {
            throw std::system_error(errno, std::system_category(),
                                  "Failed to create PID namespace");
        }

        created_ = true;
    }

    void join(pid_t pid) override {
        std::string ns_path = getNamespacePath(pid, Type::PID);
        fd_ = open(ns_path.c_str(), O_RDONLY | O_CLOEXEC);

        if (fd_ == -1) {
            throw std::system_error(errno, std::system_category(),
                                  "Failed to open PID namespace file");
        }

        if (setns(fd_, static_cast<int>(Type::PID)) == -1) {
            close(fd_);
            fd_ = -1;
            throw std::system_error(errno, std::system_category(),
                                  "Failed to join PID namespace");
        }
    }

    std::string getTypeName() const override { return "pid"; }
    Type getNamespaceType() const override { return Type::PID; }

    // PID-specific operations
    pid_t spawnProcess(const std::string& executable,
                      const std::vector<std::string>& args,
                      const std::vector<std::string>& env = {}) {
        if (!created_) {
            throw std::runtime_error("PID namespace not created");
        }

        pid_t pid = fork();
        if (pid == -1) {
            throw std::system_error(errno, std::system_category(),
                                  "Failed to fork process");
        }

        if (pid == 0) {
            // Child process - we're in the new PID namespace

            // Mount new proc filesystem
            if (mount("proc", "/proc", "proc", 0, nullptr) == -1) {
                std::cerr << "Failed to mount /proc: " << strerror(errno) << std::endl;
                _exit(1);
            }

            // Prepare argv
            std::vector<char*> argv;
            argv.push_back(const_cast<char*>(executable.c_str()));
            for (const auto& arg : args) {
                argv.push_back(const_cast<char*>(arg.c_str()));
            }
            argv.push_back(nullptr);

            // Prepare environment
            std::vector<char*> envp;
            for (const auto& env_var : env) {
                envp.push_back(const_cast<char*>(env_var.c_str()));
            }
            envp.push_back(nullptr);

            // Execute the process
            execve(executable.c_str(), argv.data(), envp.data());

            // If we reach here, exec failed
            std::cerr << "Failed to execute " << executable << ": "
                      << strerror(errno) << std::endl;
            _exit(127);
        }

        return pid;
    }

    std::vector<pid_t> listProcesses() const {
        std::vector<pid_t> processes;

        DIR* proc_dir = opendir("/proc");
        if (!proc_dir) {
            throw std::system_error(errno, std::system_category(),
                                  "Failed to open /proc directory");
        }

        struct dirent* entry;
        while ((entry = readdir(proc_dir)) != nullptr) {
            if (isdigit(entry->d_name[0])) {
                pid_t pid = std::stoi(entry->d_name);
                processes.push_back(pid);
            }
        }

        closedir(proc_dir);
        return processes;
    }

    bool isProcessInNamespace(pid_t pid) const {
        std::string ns_path = getNamespacePath(pid, Type::PID);
        return access(ns_path.c_str(), F_OK) == 0;
    }

private:
    int fd_;
    bool created_;
};
```

### 3. Network Namespace Implementation

```cpp
#include <net/if.h>
#include <linux/if_tun.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

class NetworkNamespace : public NamespaceManager {
public:
    NetworkNamespace() : fd_(-1), created_(false) {}

    ~NetworkNamespace() override {
        if (fd_ >= 0) {
            close(fd_);
        }
        cleanup();
    }

    void create() override {
        if (created_) {
            throw std::runtime_error("Network namespace already created");
        }

        if (unshare(static_cast<int>(Type::NETWORK)) == -1) {
            throw std::system_error(errno, std::system_category(),
                                  "Failed to create network namespace");
        }

        created_ = true;
        namespace_name_ = generateNamespaceName();
    }

    void join(pid_t pid) override {
        std::string ns_path = getNamespacePath(pid, Type::NETWORK);
        fd_ = open(ns_path.c_str(), O_RDONLY | O_CLOEXEC);

        if (fd_ == -1) {
            throw std::system_error(errno, std::system_category(),
                                  "Failed to open network namespace file");
        }

        if (setns(fd_, static_cast<int>(Type::NETWORK)) == -1) {
            close(fd_);
            fd_ = -1;
            throw std::system_error(errno, std::system_category(),
                                  "Failed to join network namespace");
        }
    }

    std::string getTypeName() const override { return "net"; }
    Type getNamespaceType() const override { return Type::NETWORK; }

    // Network-specific operations
    void createVethPair(const std::string& veth_name, const std::string& peer_name) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == -1) {
            throw std::system_error(errno, std::system_category(),
                                  "Failed to create socket");
        }

        struct ifreq ifr;
        memset(&ifr, 0, sizeof(ifr));
        strncpy(ifr.ifr_name, veth_name.c_str(), IFNAMSIZ - 1);

        // Create veth pair
        if (ioctl(sock, SIOCGIFINDEX, &ifr) == -1) {
            close(sock);
            throw std::system_error(errno, std::system_category(),
                                  "Failed to get interface index");
        }

        close(sock);

        // Actually create veth pair using netlink or ip command
        std::string cmd = "ip link add " + veth_name + " type veth peer name " + peer_name;
        int result = system(cmd.c_str());
        if (result != 0) {
            throw std::runtime_error("Failed to create veth pair");
        }
    }

    void moveInterfaceToNamespace(const std::string& interface_name, pid_t target_pid) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == -1) {
            throw std::system_error(errno, std::system_category(),
                                  "Failed to create socket");
        }

        struct ifreq ifr;
        memset(&ifr, 0, sizeof(ifr));
        strncpy(ifr.ifr_name, interface_name.c_str(), IFNAMSIZ - 1);
        ifr.ifr_ifindex = target_pid;

        if (ioctl(sock, SIOCSIFNETNS, &ifr) == -1) {
            close(sock);
            throw std::system_error(errno, std::system_category(),
                                  "Failed to move interface to namespace");
        }

        close(sock);
    }

    void assignIpAddress(const std::string& interface_name,
                        const std::string& ip_address,
                        const std::string& netmask) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == -1) {
            throw std::system_error(errno, std::system_category(),
                                  "Failed to create socket");
        }

        struct ifreq ifr;
        memset(&ifr, 0, sizeof(ifr));
        strncpy(ifr.ifr_name, interface_name.c_str(), IFNAMSIZ - 1);

        // Get interface index
        if (ioctl(sock, SIOCGIFINDEX, &ifr) == -1) {
            close(sock);
            throw std::system_error(errno, std::system_category(),
                                  "Failed to get interface index");
        }

        // Bring interface up
        ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
        if (ioctl(sock, SIOCSIFFLAGS, &ifr) == -1) {
            close(sock);
            throw std::system_error(errno, std::system_category(),
                                  "Failed to bring interface up");
        }

        close(sock);

        // Use ip command to assign IP address (simplified)
        std::string cmd = "ip addr add " + ip_address + "/" + netmask +
                         " dev " + interface_name;
        int result = system(cmd.c_str());
        if (result != 0) {
            throw std::runtime_error("Failed to assign IP address");
        }
    }

    std::vector<std::string> listInterfaces() const {
        std::vector<std::string> interfaces;

        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == -1) {
            throw std::system_error(errno, std::system_category(),
                                  "Failed to create socket");
        }

        FILE* fp = popen("ip link show", "r");
        if (!fp) {
            close(sock);
            throw std::runtime_error("Failed to execute ip command");
        }

        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, ":")) {
                std::string line_str(line);
                size_t colon_pos = line_str.find(':');
                if (colon_pos != std::string::npos) {
                    std::string interface = line_str.substr(colon_pos + 2);
                    interface = interface.substr(0, interface.find('@'));
                    // Trim whitespace
                    interface.erase(0, interface.find_first_not_of(" \t"));
                    interface.erase(interface.find_last_not_of(" \t") + 1);

                    if (!interface.empty() && interface != "lo") {
                        interfaces.push_back(interface);
                    }
                }
            }
        }

        pclose(fp);
        close(sock);
        return interfaces;
    }

    void createBridge(const std::string& bridge_name) {
        std::string cmd = "ip link add name " + bridge_name + " type bridge";
        int result = system(cmd.c_str());
        if (result != 0) {
            throw std::runtime_error("Failed to create bridge");
        }

        // Bring bridge up
        cmd = "ip link set " + bridge_name + " up";
        result = system(cmd.c_str());
        if (result != 0) {
            throw std::runtime_error("Failed to bring bridge up");
        }
    }

private:
    int fd_;
    bool created_;
    std::string namespace_name_;

    std::string generateNamespaceName() const {
        static std::atomic<uint64_t> counter{0};
        return "netns_" + std::to_string(counter.fetch_add(1));
    }

    void cleanup() {
        if (!namespace_name_.empty()) {
            // Cleanup network namespace
            std::string cmd = "ip netns del " + namespace_name_;
            system(cmd.c_str());
        }
    }
};
```

### 4. Mount Namespace Implementation

```cpp
class MountNamespace : public NamespaceManager {
public:
    MountNamespace() : fd_(-1), created_(false) {}

    ~MountNamespace() override {
        if (fd_ >= 0) {
            close(fd_);
        }
    }

    void create() override {
        if (created_) {
            throw std::runtime_error("Mount namespace already created");
        }

        if (unshare(static_cast<int>(Type::MOUNT)) == -1) {
            throw std::system_error(errno, std::system_category(),
                                  "Failed to create mount namespace");
        }

        created_ = true;
    }

    void join(pid_t pid) override {
        std::string ns_path = getNamespacePath(pid, Type::MOUNT);
        fd_ = open(ns_path.c_str(), O_RDONLY | O_CLOEXEC);

        if (fd_ == -1) {
            throw std::system_error(errno, std::system_category(),
                                  "Failed to open mount namespace file");
        }

        if (setns(fd_, static_cast<int>(Type::MOUNT)) == -1) {
            close(fd_);
            fd_ = -1;
            throw std::system_error(errno, std::system_category(),
                                  "Failed to join mount namespace");
        }
    }

    std::string getTypeName() const override { return "mnt"; }
    Type getNamespaceType() const override { return Type::MOUNT; }

    // Mount-specific operations
    void mountProc(const std::string& mount_point = "/proc") {
        if (mount("proc", mount_point.c_str(), "proc", 0, nullptr) == -1) {
            throw std::system_error(errno, std::system_category(),
                                  "Failed to mount proc filesystem");
        }
    }

    void mountTmpfs(const std::string& mount_point, size_t size_bytes = 0) {
        std::string options;
        if (size_bytes > 0) {
            options = "size=" + std::to_string(size_bytes);
        }

        if (mount("tmpfs", mount_point.c_str(), "tmpfs", 0, options.c_str()) == -1) {
            throw std::system_error(errno, std::system_category(),
                                  "Failed to mount tmpfs");
        }
    }

    void bindMount(const std::string& source, const std::string& target,
                   bool read_only = false) {
        // Create target directory if it doesn't exist
        std::filesystem::create_directories(target);

        unsigned long flags = MS_BIND;
        if (read_only) {
            flags |= MS_RDONLY;
        }

        if (mount(source.c_str(), target.c_str(), "bind", flags, nullptr) == -1) {
            throw std::system_error(errno, std::system_category(),
                                  "Failed to create bind mount");
        }
    }

    void remount(const std::string& mount_point, unsigned long flags) {
        if (mount(nullptr, mount_point.c_str(), nullptr, MS_REMOUNT | flags, nullptr) == -1) {
            throw std::system_error(errno, std::system_category(),
                                  "Failed to remount filesystem");
        }
    }

    void unmount(const std::string& mount_point, bool lazy = false) {
        unsigned long flags = 0;
        if (lazy) {
            flags |= MNT_DETACH;
        }

        if (umount2(mount_point.c_str(), flags) == -1) {
            throw std::system_error(errno, std::system_category(),
                                  "Failed to unmount filesystem");
        }
    }

    void makePropagationPrivate() {
        if (mount(nullptr, "/", nullptr, MS_PRIVATE | MS_REC, nullptr) == -1) {
            throw std::system_error(errno, std::system_category(),
                                  "Failed to make mount propagation private");
        }
    }

    void makeMountSlave() {
        if (mount(nullptr, "/", nullptr, MS_SLAVE | MS_REC, nullptr) == -1) {
            throw std::system_error(errno, std::system_category(),
                                  "Failed to make mount slave");
        }
    }

    struct MountInfo {
        std::string source;
        std::string target;
        std::string filesystem_type;
        std::vector<std::string> options;
    };

    std::vector<MountInfo> getMounts() const {
        std::vector<MountInfo> mounts;

        std::ifstream mounts_file("/proc/mounts");
        if (!mounts_file) {
            throw std::runtime_error("Failed to read /proc/mounts");
        }

        std::string line;
        while (std::getline(mounts_file, line)) {
            std::istringstream iss(line);
            MountInfo info;

            if (!(iss >> info.source >> info.target >> info.filesystem_type)) {
                continue;
            }

            std::string options_str;
            if (iss >> options_str) {
                std::istringstream options_stream(options_str);
                std::string option;
                while (std::getline(options_stream, option, ',')) {
                    info.options.push_back(option);
                }
            }

            mounts.push_back(info);
        }

        return mounts;
    }

private:
    int fd_;
    bool created_;
};
```

## Namespace Factory and Management

### 1. Namespace Factory Pattern

```cpp
class NamespaceFactory {
public:
    static std::unique_ptr<NamespaceManager> createNamespace(NamespaceManager::Type type) {
        switch (type) {
            case NamespaceManager::Type::PID:
                return std::make_unique<PidNamespace>();
            case NamespaceManager::Type::NETWORK:
                return std::make_unique<NetworkNamespace>();
            case NamespaceManager::Type::MOUNT:
                return std::make_unique<MountNamespace>();
            case NamespaceManager::Type::UTS:
                return std::make_unique<UtsNamespace>();
            case NamespaceManager::Type::IPC:
                return std::make_unique<IpcNamespace>();
            case NamespaceManager::Type::USER:
                return std::make_unique<UserNamespace>();
            case NamespaceManager::Type::CGROUP:
                return std::make_unique<CgroupNamespace>();
            default:
                throw std::invalid_argument("Unsupported namespace type");
        }
    }

    static std::vector<std::unique_ptr<NamespaceManager>> createNamespaces(
        const std::vector<NamespaceManager::Type>& types) {

        std::vector<std::unique_ptr<NamespaceManager>> namespaces;
        namespaces.reserve(types.size());

        for (auto type : types) {
            namespaces.push_back(createNamespace(type));
        }

        return namespaces;
    }
};
```

### 2. Container Namespace Manager

```cpp
class ContainerNamespaceManager {
public:
    struct NamespaceConfig {
        bool enable_pid = true;
        bool enable_network = true;
        bool enable_mount = true;
        bool enable_uts = true;
        bool enable_ipc = true;
        bool enable_user = true;
        bool enable_cgroup = true;

        std::string hostname;
        std::string domainname;
        std::vector<std::string> bind_mounts;
        std::vector<std::string> read_only_mounts;
    };

    explicit ContainerNamespaceManager(const NamespaceConfig& config)
        : config_(config) {
        initializeNamespaces();
    }

    void setupNamespaces() {
        for (auto& [type, ns] : namespaces_) {
            ns->create();
        }

        // Configure specific namespace settings
        configureNamespaces();
    }

    void joinNamespaces(pid_t pid) {
        for (auto& [type, ns] : namespaces_) {
            ns->join(pid);
        }
    }

    PidNamespace* getPidNamespace() {
        return dynamic_cast<PidNamespace*>(namespaces_[NamespaceManager::Type::PID].get());
    }

    NetworkNamespace* getNetworkNamespace() {
        return dynamic_cast<NetworkNamespace*>(namespaces_[NamespaceManager::Type::NETWORK].get());
    }

    MountNamespace* getMountNamespace() {
        return dynamic_cast<MountNamespace*>(namespaces_[NamespaceManager::Type::MOUNT].get());
    }

    bool hasNamespace(NamespaceManager::Type type) const {
        return namespaces_.find(type) != namespaces_.end();
    }

private:
    NamespaceConfig config_;
    std::unordered_map<NamespaceManager::Type, std::unique_ptr<NamespaceManager>> namespaces_;

    void initializeNamespaces() {
        if (config_.enable_pid) {
            namespaces_[NamespaceManager::Type::PID] =
                NamespaceFactory::createNamespace(NamespaceManager::Type::PID);
        }
        if (config_.enable_network) {
            namespaces_[NamespaceManager::Type::NETWORK] =
                NamespaceFactory::createNamespace(NamespaceManager::Type::NETWORK);
        }
        if (config_.enable_mount) {
            namespaces_[NamespaceManager::Type::MOUNT] =
                NamespaceFactory::createNamespace(NamespaceManager::Type::MOUNT);
        }
        if (config_.enable_uts) {
            namespaces_[NamespaceManager::Type::UTS] =
                NamespaceFactory::createNamespace(NamespaceManager::Type::UTS);
        }
        if (config_.enable_ipc) {
            namespaces_[NamespaceManager::Type::IPC] =
                NamespaceFactory::createNamespace(NamespaceManager::Type::IPC);
        }
        if (config_.enable_user) {
            namespaces_[NamespaceManager::Type::USER] =
                NamespaceFactory::createNamespace(NamespaceManager::Type::USER);
        }
        if (config_.enable_cgroup) {
            namespaces_[NamespaceManager::Type::CGROUP] =
                NamespaceFactory::createNamespace(NamespaceManager::Type::CGROUP);
        }
    }

    void configureNamespaces() {
        // Configure UTS namespace
        if (auto* uts_ns = dynamic_cast<UtsNamespace*>(namespaces_[NamespaceManager::Type::UTS].get())) {
            if (!config_.hostname.empty()) {
                uts_ns->setHostname(config_.hostname);
            }
            if (!config_.domainname.empty()) {
                uts_ns->setDomainname(config_.domainname);
            }
        }

        // Configure Mount namespace
        if (auto* mount_ns = getMountNamespace()) {
            mount_ns->makePropagationPrivate();
            mount_ns->mountProc();

            // Setup bind mounts
            for (const auto& mount : config_.bind_mounts) {
                size_t colon_pos = mount.find(':');
                if (colon_pos != std::string::npos) {
                    std::string source = mount.substr(0, colon_pos);
                    std::string target = mount.substr(colon_pos + 1);
                    mount_ns->bindMount(source, target);
                }
            }

            // Setup read-only mounts
            for (const auto& mount : config_.read_only_mounts) {
                size_t colon_pos = mount.find(':');
                if (colon_pos != std::string::npos) {
                    std::string source = mount.substr(0, colon_pos);
                    std::string target = mount.substr(colon_pos + 1);
                    mount_ns->bindMount(source, target, true);
                }
            }
        }
    }
};
```

## Advanced Namespace Operations

### 1. Namespace Discovery and Inspection

```cpp
class NamespaceInspector {
public:
    struct NamespaceInfo {
        std::string type;
        std::string path;
        ino_t inode;
        pid_t owner_pid;
        std::chrono::system_clock::time_point created_at;
    };

    static std::vector<NamespaceInfo> getProcessNamespaces(pid_t pid) {
        std::vector<NamespaceInfo> namespaces;

        std::string proc_ns_path = "/proc/" + std::to_string(pid) + "/ns";

        for (const auto& entry : std::filesystem::directory_iterator(proc_ns_path)) {
            if (entry.is_symlink()) {
                NamespaceInfo info;
                info.type = entry.path().filename().string();
                info.path = entry.path().string();

                // Get inode and device
                struct stat st;
                if (stat(entry.path().c_str(), &st) == 0) {
                    info.inode = st.st_ino;
                    info.owner_pid = pid;
                    info.created_at = std::chrono::system_clock::from_time_t(st.st_ctime);
                }

                namespaces.push_back(info);
            }
        }

        return namespaces;
    }

    static bool areProcessesInSameNamespace(pid_t pid1, pid_t pid2,
                                           NamespaceManager::Type type) {
        try {
            auto namespaces1 = getProcessNamespaces(pid1);
            auto namespaces2 = getProcessNamespaces(pid2);

            std::string type_str = namespaceTypeToString(type);

            auto ns1_it = std::find_if(namespaces1.begin(), namespaces1.end(),
                [&type_str](const NamespaceInfo& info) {
                    return info.type == type_str;
                });

            auto ns2_it = std::find_if(namespaces2.begin(), namespaces2.end(),
                [&type_str](const NamespaceInfo& info) {
                    return info.type == type_str;
                });

            if (ns1_it != namespaces1.end() && ns2_it != namespaces2.end()) {
                return ns1_it->inode == ns2_it->inode;
            }

            return false;
        } catch (...) {
            return false;
        }
    }

    static pid_t getNamespaceOwner(const std::string& namespace_path) {
        struct stat st;
        if (stat(namespace_path.c_str(), &st) == 0) {
            return st.st_uid;
        }
        return -1;
    }

private:
    static std::string namespaceTypeToString(NamespaceManager::Type type) {
        switch (type) {
            case NamespaceManager::Type::PID: return "pid";
            case NamespaceManager::Type::NETWORK: return "net";
            case NamespaceManager::Type::MOUNT: return "mnt";
            case NamespaceManager::Type::UTS: return "uts";
            case NamespaceManager::Type::IPC: return "ipc";
            case NamespaceManager::Type::USER: return "user";
            case NamespaceManager::Type::CGROUP: return "cgroup";
            default: return "";
        }
    }
};
```

### 2. Namespace Cleanup and Resource Management

```cpp
class NamespaceCleaner {
public:
    static void cleanupOrphanedNamespaces() {
        std::unordered_set<ino_t> active_inodes;

        // Collect all active namespace inodes
        DIR* proc_dir = opendir("/proc");
        if (!proc_dir) {
            return;
        }

        struct dirent* entry;
        while ((entry = readdir(proc_dir)) != nullptr) {
            if (isdigit(entry->d_name[0])) {
                pid_t pid = std::stoi(entry->d_name);
                try {
                    auto namespaces = NamespaceInspector::getProcessNamespaces(pid);
                    for (const auto& ns : namespaces) {
                        active_inodes.insert(ns.inode);
                    }
                } catch (...) {
                    // Process might have disappeared
                    continue;
                }
            }
        }
        closedir(proc_dir);

        // Find and cleanup orphaned namespaces
        cleanupOrphanedNetworkNamespaces(active_inodes);
    }

private:
    static void cleanupOrphanedNetworkNamespaces(const std::unordered_set<ino_t>& active_inodes) {
        // Check /var/run/netns for orphaned network namespaces
        std::string netns_dir = "/var/run/netns";

        if (!std::filesystem::exists(netns_dir)) {
            return;
        }

        for (const auto& entry : std::filesystem::directory_iterator(netns_dir)) {
            if (entry.is_regular_file()) {
                struct stat st;
                if (stat(entry.path().c_str(), &st) == 0) {
                    if (active_inodes.find(st.st_ino) == active_inodes.end()) {
                        // This namespace is orphaned
                        std::cout << "Cleaning up orphaned namespace: "
                                  << entry.path().filename().string() << std::endl;

                        std::string cmd = "ip netns del " + entry.path().filename().string();
                        system(cmd.c_str());
                    }
                }
            }
        }
    }
};
```

## Usage Example: Complete Container Creation

```cpp
class ContainerCreator {
public:
    struct ContainerConfig {
        std::string id;
        std::string image;
        std::vector<std::string> command;
        std::string hostname = "container";
        std::vector<std::string> bind_mounts;
        std::vector<std::string> network_interfaces;
    };

    pid_t createContainer(const ContainerConfig& config) {
        // Setup namespace configuration
        ContainerNamespaceManager::NamespaceConfig ns_config;
        ns_config.hostname = config.hostname;
        ns_config.bind_mounts = config.bind_mounts;

        // Create namespace manager
        auto ns_manager = std::make_unique<ContainerNamespaceManager>(ns_config);

        // Fork to create container process
        pid_t pid = fork();
        if (pid == -1) {
            throw std::system_error(errno, std::system_category(),
                                  "Failed to fork container process");
        }

        if (pid == 0) {
            // Child process - setup namespaces
            try {
                ns_manager->setupNamespaces();

                // Execute container command
                executeContainerCommand(config);
            } catch (const std::exception& e) {
                std::cerr << "Failed to setup container namespaces: "
                          << e.what() << std::endl;
                _exit(1);
            }
        }

        return pid;
    }

private:
    void executeContainerCommand(const ContainerConfig& config) {
        // Prepare argv
        std::vector<char*> argv;
        for (const auto& cmd : config.command) {
            argv.push_back(const_cast<char*>(cmd.c_str()));
        }
        argv.push_back(nullptr);

        // Execute the command
        execvp(config.command[0].c_str(), argv.data());

        // If we reach here, exec failed
        std::cerr << "Failed to execute container command: "
                  << strerror(errno) << std::endl;
        _exit(127);
    }
};
```

## Security Considerations

### 1. Namespace Security Best Practices

```cpp
class NamespaceSecurity {
public:
    static void applySecureDefaults(ContainerNamespaceManager& ns_manager) {
        // Ensure proper user namespace setup for privilege separation
        if (auto* user_ns = dynamic_cast<UserNamespace*>(
                ns_manager.namespaces_[NamespaceManager::Type::USER].get())) {
            user_ns->setupRootlessMapping();
        }

        // Mount sensitive filesystems as read-only
        if (auto* mount_ns = ns_manager.getMountNamespace()) {
            mount_ns->bindMount("/proc/sys", "/proc/sys", true);
            mount_ns->bindMount("/sys", "/sys", true);
        }

        // Restrict network access if not needed
        if (auto* net_ns = ns_manager.getNetworkNamespace()) {
            // Only allow loopback interface by default
            net_ns->createVethPair("eth0", "container-eth0");
            // Setup restrictive firewall rules
        }
    }

    static void validateNamespaceConfiguration(const ContainerNamespaceManager::NamespaceConfig& config) {
        // Security validation logic
        if (config.bind_mounts.empty()) {
            // Warning: No filesystem isolation
        }

        // Check for potentially dangerous mounts
        for (const auto& mount : config.bind_mounts) {
            if (mount.find("/etc") != std::string::npos) {
                // Warning: Mounting system configuration files
            }
            if (mount.find("/proc") != std::string::npos) {
                // Warning: Mounting proc filesystem
            }
        }
    }
};
```

## Performance Optimization

### 1. Namespace Creation Optimization

```cpp
class OptimizedNamespaceManager {
public:
    // Batch namespace creation for better performance
    static void createNamespacesBatch(const std::vector<NamespaceManager::Type>& types) {
        // Create all namespaces in a single unshare call when possible
        int flags = 0;
        for (auto type : types) {
            flags |= static_cast<int>(type);
        }

        if (unshare(flags) == -1) {
            // Fall back to individual namespace creation
            for (auto type : types) {
                try {
                    auto ns = NamespaceFactory::createNamespace(type);
                    ns->create();
                } catch (const std::exception& e) {
                    std::cerr << "Failed to create namespace "
                              << static_cast<int>(type) << ": " << e.what() << std::endl;
                }
            }
        }
    }

    // Pre-allocate namespace resources
    static void preallocateNamespaceResources() {
        // Pre-create network namespace pools
        // Pre-allocate PID namespace structures
        // Cache mount namespace templates
    }
};
```

## Conclusion

Linux namespaces provide the fundamental isolation mechanisms that make container technology possible. The C++ implementation presented in this article demonstrates how to create robust, secure, and efficient namespace management for container runtimes.

Key takeaways:

1. **RAII Pattern**: Ensures proper resource cleanup and exception safety
2. **Type Safety**: Strong typing prevents namespace configuration errors
3. **Performance**: Efficient namespace creation and management
4. **Security**: Proper privilege separation and isolation
5. **Flexibility**: Configurable namespace combinations for different use cases

The namespace management system forms the foundation of our docker-cpp implementation, providing the isolation needed for secure multi-tenant container execution.

## Next Steps

In our next article, "Resource Control: Cgroups Management System," we'll explore how to implement comprehensive resource control using Linux control groups, building on the namespace foundation established here.

---

**Previous Article**: [Filesystem Abstraction: Union Filesystems in C++](./05-union-filesystem-implementation.md)
**Next Article**: [Resource Control: Cgroups Management System](./07-resource-control-cgroups.md)
**Series Index**: [Table of Contents](./00-table-of-contents.md)
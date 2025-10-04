# Docker C++ Implementation - Windows and macOS: Cross-Platform Container Challenges

*Implementing container runtime support for Windows and macOS platforms*

## Table of Contents
1. [Introduction](#introduction)
2. [Cross-Platform Architecture Overview](#cross-platform-architecture-overview)
3. [Windows Container Support](#windows-container-support)
4. [macOS Container Support](#macos-container-support)
5. [Linux VM Management](#linux-vm-management)
6. [File Sharing and Performance](#file-sharing-and-performance)
7. [Network Virtualization](#network-virtualization)
8. [Complete Implementation](#complete-implementation)
9. [Performance Optimizations](#performance-optimizations)
10. [Platform-Specific Considerations](#platform-specific-considerations)

## Introduction

Running Linux containers on Windows and macOS presents unique challenges due to fundamental differences in operating system architecture. Unlike Linux where containers share the host kernel, Windows and macOS require virtualization approaches to provide Linux compatibility.

This article explores the implementation of cross-platform container support in our Docker C++ implementation, covering:

- **Hyper-V Integration**: Windows hypervisor-based container isolation
- **Linux VM Management**: Lightweight virtual machines for macOS/Linux compatibility
- **File Sharing**: Efficient cross-platform file system sharing
- **Network Virtualization**: Container networking across platform boundaries
- **Performance Optimization**: Minimizing virtualization overhead

## Cross-Platform Architecture Overview

### Platform Abstraction Layer

```cpp
// Cross-platform container architecture
// ┌─────────────────────────────────────────────────────────┐
// │                 Docker Engine (C++)                     │
// │                                                         │
// │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐     │
// │  │   Linux     │  │   Windows   │  │    macOS    │     │
// │  │  Runtime    │  │  Runtime    │  │   Runtime   │     │
// │  └─────────────┘  └─────────────┘  └─────────────┘     │
// └─────────────────────────────────────────────────────────┘
//          │                   │                   │
//          └───────────────────┼───────────────────┘
//                              │
//          ┌─────────────────────────────────────┐
//          │      Platform Abstraction Layer     │
//          │                                     │
//          │  • Virtualization Backend           │
//          │  • File System Bridge               │
//          │  • Network Proxy                    │
//          │  • Resource Management              │
//          └─────────────────────────────────────┘
//                              │
//          ┌─────────────────────────────────────┐
//          │     Host Operating System           │
//          │  (Linux / Windows / macOS)          │
//          └─────────────────────────────────────┘
```

### Core Abstraction Interfaces

```cpp
#pragma once
#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <unordered_map>
#include <future>

namespace cross_platform {

// Platform detection utilities
enum class Platform {
    LINUX,
    WINDOWS,
    MACOS,
    UNKNOWN
};

inline Platform getCurrentPlatform() {
#ifdef _WIN32
    return Platform::WINDOWS;
#elif __APPLE__
    return Platform::MACOS;
#elif __linux__
    return Platform::LINUX;
#else
    return Platform::UNKNOWN;
#endif
}

// Virtualization backend types
enum class VirtualizationBackend {
    NONE,           // Native Linux containers
    HYPER_V,        // Windows Hyper-V
    QEMU,           // QEMU-based virtualization
    VIRTUAL_BOX,    // VirtualBox (legacy)
    WSL2,          // Windows Subsystem for Linux 2
    HYPER_KIT      // macOS HyperKit
};

// Platform-specific configuration
struct PlatformConfig {
    Platform platform;
    VirtualizationBackend backend;

    // Resource limits
    size_t defaultMemoryMB = 2048;
    size_t defaultCpuCount = 2;
    size_t maxContainers = 100;

    // File sharing
    std::string defaultSharePath;
    bool enable9pSharing = true;
    bool enableVirtioFs = false;

    // Networking
    std::string networkMode = "nat";
    std::vector<std::string> dnsServers;
    bool enableIPv6 = false;

    // Performance tuning
    bool enableKsm = false;
    bool enableHugePages = false;
    size_t ioThreadCount = 4;
};

// Virtual machine interface
class IVirtualMachine {
public:
    virtual ~IVirtualMachine() = default;

    // VM lifecycle
    virtual bool start() = 0;
    virtual bool stop() = 0;
    virtual bool pause() = 0;
    virtual bool resume() = 0;
    virtual bool isRunning() const = 0;

    // Resource management
    virtual bool setMemory(size_t memoryMB) = 0;
    virtual bool setCpuCount(size_t cpuCount) = 0;
    virtual bool addDisk(const std::string& imagePath, size_t sizeGB) = 0;
    virtual bool mountShared(const std::string& hostPath, const std::string& guestPath) = 0;

    // Network configuration
    virtual bool addNetworkInterface(const std::string& networkName) = 0;
    virtual bool configureNetwork(const std::string& ipAddress,
                                 const std::string& gateway) = 0;
    virtual std::string getIpAddress() const = 0;

    // Guest operations
    virtual bool executeCommand(const std::string& command, std::string& output) = 0;
    virtual bool copyFileToGuest(const std::string& hostPath,
                                const std::string& guestPath) = 0;
    virtual bool copyFileFromGuest(const std::string& guestPath,
                                  const std::string& hostPath) = 0;

    // Monitoring
    virtual size_t getMemoryUsage() const = 0;
    virtual double getCpuUsage() const = 0;
    virtual size_t getDiskUsage() const = 0;
};

// File system bridge interface
class IFileSystemBridge {
public:
    virtual ~IFileSystemBridge() = default;

    // Volume operations
    virtual bool createSharedVolume(const std::string& volumeName,
                                   const std::string& hostPath) = 0;
    virtual bool deleteSharedVolume(const std::string& volumeName) = 0;
    virtual bool mountVolume(const std::string& volumeName,
                            const std::string& mountPoint) = 0;
    virtual bool unmountVolume(const std::string& mountPoint) = 0;

    // File operations
    virtual bool copyFile(const std::string& source, const std::string& destination) = 0;
    virtual bool syncDirectory(const std::string& source, const std::string& destination) = 0;
    virtual std::vector<std::string> listFiles(const std::string& path) = 0;

    // Performance optimization
    virtual bool enableCaching(bool enabled) = 0;
    virtual bool enableAsyncOperations(bool enabled) = 0;
    virtual void flushCache() = 0;
};

// Network proxy interface
class INetworkProxy {
public:
    virtual ~INetworkProxy() = default;

    // Network management
    virtual bool createNetwork(const std::string& networkName,
                              const std::unordered_map<std::string, std::string>& options) = 0;
    virtual bool deleteNetwork(const std::string& networkName) = 0;
    virtual bool connectContainer(const std::string& networkId,
                                 const std::string& containerId) = 0;
    virtual bool disconnectContainer(const std::string& networkId,
                                    const std::string& containerId) = 0;

    // Port forwarding
    virtual bool exposePort(const std::string& containerId,
                           uint16_t containerPort,
                           uint16_t hostPort) = 0;
    virtual bool hidePort(const std::string& containerId,
                         uint16_t hostPort) = 0;

    // DNS management
    virtual bool addDnsEntry(const std::string& containerId,
                            const std::string& hostname,
                            const std::string& ipAddress) = 0;
    virtual bool removeDnsEntry(const std::string& containerId,
                               const std::string& hostname) = 0;
};

// Platform factory interface
class IPlatformFactory {
public:
    virtual ~IPlatformFactory() = default;

    virtual std::unique_ptr<IVirtualMachine> createVirtualMachine(
        const PlatformConfig& config) = 0;
    virtual std::unique_ptr<IFileSystemBridge> createFileSystemBridge(
        const PlatformConfig& config) = 0;
    virtual std::unique_ptr<INetworkProxy> createNetworkProxy(
        const PlatformConfig& config) = 0;

    virtual bool initialize() = 0;
    virtual void cleanup() = 0;
    virtual Platform getPlatform() const = 0;
    virtual VirtualizationBackend getBackend() const = 0;
};

} // namespace cross_platform
```

## Windows Container Support

### Hyper-V Integration

```cpp
#include <windows.h>
#include <virtdisk.h>
#include <vmcompute.h>
#include <comdef.h>
#include <future>

namespace cross_platform {

// Windows Hyper-V virtual machine implementation
class HyperVVirtualMachine : public IVirtualMachine {
private:
    std::string name_;
    std::string vhdPath_;
    HANDLE vm_handle_ = INVALID_HANDLE_VALUE;
    PlatformConfig config_;
    std::atomic<bool> running_{false};
    mutable std::mutex mutex_;

    // Hyper-V specific handles
    MOUNT_VIRTUAL_DISK_PARAMETERS mount_params_;
    HANDLE disk_handle_ = INVALID_HANDLE_VALUE;

public:
    HyperVVirtualMachine(const std::string& name, const PlatformConfig& config)
        : name_(name), config_(config) {
        vhdPath_ = config.defaultSharePath + "/" + name + ".vhdx";
    }

    ~HyperVVirtualMachine() {
        stop();
    }

    bool start() override {
        std::lock_guard<std::mutex> lock(mutex_);

        if (running_) return true;

        // Create VHD if it doesn't exist
        if (!std::filesystem::exists(vhdPath_)) {
            if (!createVirtualDisk()) {
                return false;
            }
        }

        // Mount the virtual disk
        if (!mountVirtualDisk()) {
            return false;
        }

        // Start the VM using HCS (Host Compute Service)
        if (!startHcsVm()) {
            unmountVirtualDisk();
            return false;
        }

        running_ = true;
        return true;
    }

    bool stop() override {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!running_) return true;

        // Stop HCS VM
        stopHcsVm();

        // Unmount virtual disk
        unmountVirtualDisk();

        running_ = false;
        return true;
    }

    bool pause() override {
        if (!running_) return false;

        // Save VM state using HCS
        HCS_OPERATION operation = nullptr;
        HRESULT result = HcsOperationCreate(&operation, nullptr, nullptr);
        if (SUCCEEDED(result)) {
            result = HcsSaveVm(vm_handle_, operation);
            HcsOperationWait(operation, nullptr, INFINITE);
            HcsOperationClose(operation);
        }

        return SUCCEEDED(result);
    }

    bool resume() override {
        if (!running_) return false;

        // Restore VM state using HCS
        HCS_OPERATION operation = nullptr;
        HRESULT result = HcsOperationCreate(&operation, nullptr, nullptr);
        if (SUCCEEDED(result)) {
            result = HcsRestoreVm(vm_handle_, operation);
            HcsOperationWait(operation, nullptr, INFINITE);
            HcsOperationClose(operation);
        }

        return SUCCEEDED(result);
    }

    bool isRunning() const override {
        return running_.load();
    }

    bool setMemory(size_t memoryMB) override {
        config_.defaultMemoryMB = memoryMB;

        if (running_) {
            // Update VM memory configuration
            HCS_OPERATION operation = nullptr;
            HRESULT result = HcsOperationCreate(&operation, nullptr, nullptr);
            if (SUCCEEDED(result)) {
                std::string config = "{\"MemorySizeInMB\":" + std::to_string(memoryMB) + "}";
                result = HcsModifyVm(vm_handle_, config.c_str(), operation);
                HcsOperationWait(operation, nullptr, INFINITE);
                HcsOperationClose(operation);
            }
            return SUCCEEDED(result);
        }

        return true;
    }

    bool setCpuCount(size_t cpuCount) override {
        config_.defaultCpuCount = cpuCount;
        return true;
    }

    bool addDisk(const std::string& imagePath, size_t sizeGB) override {
        // Create additional VHD and attach to VM
        std::string diskPath = config_.defaultSharePath + "/" +
                              std::filesystem::path(imagePath).filename().string();

        if (!createVirtualDisk(diskPath, sizeGB)) {
            return false;
        }

        // Attach disk to VM
        HCS_OPERATION operation = nullptr;
        HRESULT result = HcsOperationCreate(&operation, nullptr, nullptr);
        if (SUCCEEDED(result)) {
            std::string config = "{\"Path\":\"" + diskPath + "\"}";
            result = HcsAddVmDisk(vm_handle_, config.c_str(), operation);
            HcsOperationWait(operation, nullptr, INFINITE);
            HcsOperationClose(operation);
        }

        return SUCCEEDED(result);
    }

    bool mountShared(const std::string& hostPath, const std::string& guestPath) override {
        // Use SMB shares for Windows guests
        std::string shareName = "docker-share-" + std::to_string(std::hash<std::string>{}(hostPath));

        // Create SMB share
        std::string command = "net share " + shareName + "=\"" + hostPath + "\" /grant:Everyone,FULL";
        int result = std::system(command.c_str());

        if (result == 0) {
            // Mount share in guest
            std::string mountCommand = "mount -t cifs //" + getLocalIpAddress() +
                                     "/" + shareName + " " + guestPath +
                                     " -o guest,uid=1000,gid=1000";
            return executeCommand(mountCommand);
        }

        return false;
    }

    bool addNetworkInterface(const std::string& networkName) override {
        HCS_OPERATION operation = nullptr;
        HRESULT result = HcsOperationCreate(&operation, nullptr, nullptr);
        if (SUCCEEDED(result)) {
            std::string config = "{\"NetworkAdapterName\":\"" + networkName + "\"}";
            result = HcsAddVmNetworkAdapter(vm_handle_, config.c_str(), operation);
            HcsOperationWait(operation, nullptr, INFINITE);
            HcsOperationClose(operation);
        }

        return SUCCEEDED(result);
    }

    bool configureNetwork(const std::string& ipAddress, const std::string& gateway) override {
        std::string command = "ip addr add " + ipAddress + "/24 dev eth0 && " +
                             "ip route add default via " + gateway;
        return executeCommand(command);
    }

    std::string getIpAddress() const override {
        std::string output;
        if (executeCommand("ip route get 8.8.8.8 | awk '{print $7; exit}'", output)) {
            // Parse IP from output
            size_t start = output.find_first_not_of(" \t\n");
            if (start != std::string::npos) {
                size_t end = output.find_first_of(" \t\n", start);
                return output.substr(start, end - start);
            }
        }
        return "";
    }

    bool executeCommand(const std::string& command, std::string& output) override {
        if (!running_) return false;

        HCS_OPERATION operation = nullptr;
        HRESULT result = HcsOperationCreate(&operation, nullptr, nullptr);
        if (SUCCEEDED(result)) {
            HCS_PROCESS_INFORMATION processInfo;
            std::string commandLine = "cmd.exe /c " + command;

            result = HcsCreateProcess(vm_handle_, commandLine.c_str(),
                                     nullptr, nullptr, nullptr,
                                     &operation, &processInfo);

            if (SUCCEEDED(result)) {
                HcsOperationWait(operation, nullptr, INFINITE);

                // Get process output
                PWSTR outputBuffer = nullptr;
                HcsGetProcessOutput(processInfo, &outputBuffer);

                if (outputBuffer) {
                    std::wstring wideOutput(outputBuffer);
                    output = std::string(wideOutput.begin(), wideOutput.end());
                    CoTaskMemFree(outputBuffer);
                }

                HcsCloseProcess(processInfo);
            }

            HcsOperationClose(operation);
        }

        return SUCCEEDED(result);
    }

    bool copyFileToGuest(const std::string& hostPath, const std::string& guestPath) override {
        // Use HCS CopyFileToVm if available, otherwise SMB share
        HCS_OPERATION operation = nullptr;
        HRESULT result = HcsOperationCreate(&operation, nullptr, nullptr);
        if (SUCCEEDED(result)) {
            result = HcsCopyFileToVm(vm_handle_, hostPath.c_str(),
                                   guestPath.c_str(), operation);
            HcsOperationWait(operation, nullptr, INFINITE);
            HcsOperationClose(operation);
        }

        return SUCCEEDED(result);
    }

    bool copyFileFromGuest(const std::string& guestPath, const std::string& hostPath) override {
        HCS_OPERATION operation = nullptr;
        HRESULT result = HcsOperationCreate(&operation, nullptr, nullptr);
        if (SUCCEEDED(result)) {
            result = HcsCopyFileFromVm(vm_handle_, guestPath.c_str(),
                                     hostPath.c_str(), operation);
            HcsOperationWait(operation, nullptr, INFINITE);
            HcsOperationClose(operation);
        }

        return SUCCEEDED(result);
    }

    size_t getMemoryUsage() const override {
        // Get VM memory statistics from Hyper-V
        return config_.defaultMemoryMB;
    }

    double getCpuUsage() const override {
        // Get VM CPU statistics from Hyper-V
        return 0.0; // Placeholder
    }

    size_t getDiskUsage() const override {
        // Get VM disk statistics
        return 0; // Placeholder
    }

private:
    bool createVirtualDisk() {
        return createVirtualDisk(vhdPath_, 32); // Default 32GB
    }

    bool createVirtualDisk(const std::string& path, size_t sizeGB) {
        VIRTUAL_DISK_TYPE diskType = VIRTUAL_DISK_TYPE_DYNAMIC;
        VIRTUAL_STORAGE_TYPE storageType = {VIRTUAL_STORAGE_TYPE_DEVICE_VHDX, 0};

        CREATE_VIRTUAL_DISK_PARAMETERS params = {};
        params.Version = CREATE_VIRTUAL_DISK_VERSION_2;
        params.Version2.MaximumSize = static_cast<ULONG64>(sizeGB) * 1024 * 1024 * 1024;
        params.Version2.BlockSizeInBytes = 2 * 1024 * 1024; // 2MB blocks

        HANDLE handle = INVALID_HANDLE_VALUE;
        HRESULT result = CreateVirtualDisk(&storageType, nullptr,
                                         VIRTUAL_DISK_ACCESS_ALL,
                                         &params, nullptr, 0, nullptr,
                                         &handle);

        if (SUCCEEDED(result)) {
            CloseHandle(handle);
            return true;
        }

        return false;
    }

    bool mountVirtualDisk() {
        ZeroMemory(&mount_params_, sizeof(mount_params_));
        mount_params_.Version = MOUNT_VIRTUAL_DISK_VERSION_1;
        mount_params_.Version1.RWDepth = 1;

        HRESULT result = MountVirtualDisk(disk_handle_, &mount_params_,
                                         MOUNT_VIRTUAL_DISK_FLAG_NONE, 0,
                                         nullptr, nullptr);

        return SUCCEEDED(result);
    }

    bool unmountVirtualDisk() {
        if (disk_handle_ != INVALID_HANDLE_VALUE) {
            DetachVirtualDisk(disk_handle_, nullptr, DETACH_VIRTUAL_DISK_FLAG_NONE, 0);
            CloseHandle(disk_handle_);
            disk_handle_ = INVALID_HANDLE_VALUE;
        }
        return true;
    }

    bool startHcsVm() {
        // Configure VM settings
        std::string vmConfig = createVmConfiguration();

        HCS_OPERATION operation = nullptr;
        HRESULT result = HcsOperationCreate(&operation, nullptr, nullptr);
        if (SUCCEEDED(result)) {
            result = HcsCreateComputeSystem(name_.c_str(), vmConfig.c_str(),
                                          operation, &vm_handle_);

            if (SUCCEEDED(result)) {
                HcsOperationWait(operation, nullptr, INFINITE);

                // Start the VM
                result = HcsStartComputeSystem(vm_handle_, operation);
                HcsOperationWait(operation, nullptr, INFINITE);
            }

            HcsOperationClose(operation);
        }

        return SUCCEEDED(result);
    }

    bool stopHcsVm() {
        if (vm_handle_ != INVALID_HANDLE_VALUE) {
            HCS_OPERATION operation = nullptr;
            HRESULT result = HcsOperationCreate(&operation, nullptr, nullptr);
            if (SUCCEEDED(result)) {
                HcsShutdownComputeSystem(vm_handle_, operation,
                                       HCS_SHUTDOWN_FORCE_SHUTDOWN);
                HcsOperationWait(operation, nullptr, INFINITE);

                HcsCloseComputeSystem(vm_handle_);
                HcsOperationClose(operation);
            }
            vm_handle_ = INVALID_HANDLE_VALUE;
        }
        return true;
    }

    std::string createVmConfiguration() {
        // Create VM configuration JSON for HCS
        return R"({
            "ProcessorCount": )" + std::to_string(config_.defaultCpuCount) + R"(,
            "MemorySizeInMB": )" + std::to_string(config_.defaultMemoryMB) + R"(,
            "VirtualDiskPaths": [")" + vhdPath_ + R"("],
            "Networking": {
                "NetworkAdapters": [
                    {
                        "NetworkName": "Default Switch",
                        "AllocationMethod": "Dynamic"
                    }
                ]
            }
        })";
    }

    std::string getLocalIpAddress() const {
        // Get local IP address for SMB sharing
        return "127.0.0.1"; // Simplified
    }

    bool executeCommand(const std::string& command) {
        std::string output;
        return executeCommand(command, output);
    }
};

// Windows platform factory
class WindowsPlatformFactory : public IPlatformFactory {
private:
    PlatformConfig config_;
    bool initialized_ = false;

public:
    WindowsPlatformFactory() {
        config_.platform = Platform::WINDOWS;
        config_.backend = VirtualizationBackend::HYPER_V;
        config_.defaultSharePath = "C:\\ProgramData\\Docker\\VHDs";
        config_.defaultMemoryMB = 4096;
        config_.defaultCpuCount = 4;
        config_.enableVirtioFs = false;
        config_.networkMode = "nat";
    }

    bool initialize() override {
        if (initialized_) return true;

        // Check Hyper-V availability
        if (!checkHyperVAvailability()) {
            return false;
        }

        // Initialize HCS
        HRESULT result = HcsInitialize();
        if (FAILED(result)) {
            return false;
        }

        // Create default directories
        std::filesystem::create_directories(config_.defaultSharePath);

        initialized_ = true;
        return true;
    }

    void cleanup() override {
        if (initialized_) {
            HcsUninitialize();
            initialized_ = false;
        }
    }

    std::unique_ptr<IVirtualMachine> createVirtualMachine(
        const PlatformConfig& config) override {
        if (!initialized_) return nullptr;

        // Merge with factory config
        PlatformConfig vmConfig = config_;
        vmConfig.defaultMemoryMB = config.defaultMemoryMB > 0 ?
                                 config.defaultMemoryMB : vmConfig.defaultMemoryMB;
        vmConfig.defaultCpuCount = config.defaultCpuCount > 0 ?
                                config.defaultCpuCount : vmConfig.defaultCpuCount;

        return std::make_unique<HyperVVirtualMachine>("docker-vm", vmConfig);
    }

    std::unique_ptr<IFileSystemBridge> createFileSystemBridge(
        const PlatformConfig& config) override {
        // Create SMB-based file system bridge
        return nullptr; // Implementation would go here
    }

    std::unique_ptr<INetworkProxy> createNetworkProxy(
        const PlatformConfig& config) override {
        // Create Hyper-V network proxy
        return nullptr; // Implementation would go here
    }

    Platform getPlatform() const override {
        return Platform::WINDOWS;
    }

    VirtualizationBackend getBackend() const override {
        return VirtualizationBackend::HYPER_V;
    }

private:
    bool checkHyperVAvailability() {
        // Check if Hyper-V is installed and running
        SERVICE_STATUS serviceStatus;
        SC_HANDLE scManager = OpenSCManager(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (!scManager) return false;

        SC_HANDLE service = OpenService(scManager, L"vmms", SERVICE_QUERY_STATUS);
        if (!service) {
            CloseServiceHandle(scManager);
            return false;
        }

        bool available = QueryServiceStatus(service, &serviceStatus) &&
                        serviceStatus.dwCurrentState == SERVICE_RUNNING;

        CloseServiceHandle(service);
        CloseServiceHandle(scManager);

        return available;
    }
};

} // namespace cross_platform
```

## macOS Container Support

### HyperKit and QEMU Implementation

```cpp
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

namespace cross_platform {

// macOS HyperKit-based virtual machine
class HyperKitVirtualMachine : public IVirtualMachine {
private:
    std::string name_;
    std::string vmDir_;
    PlatformConfig config_;
    pid_t hyperkit_pid_ = -1;
    int control_socket_ = -1;
    std::atomic<bool> running_{false};
    mutable std::mutex mutex_;

public:
    HyperKitVirtualMachine(const std::string& name, const PlatformConfig& config)
        : name_(name), config_(config) {
        vmDir_ = config_.defaultSharePath + "/" + name;
    }

    ~HyperKitVirtualMachine() {
        stop();
    }

    bool start() override {
        std::lock_guard<std::mutex> lock(mutex_);

        if (running_) return true;

        // Create VM directory
        std::filesystem::create_directories(vmDir_);

        // Create disk image
        if (!createDiskImage()) {
            return false;
        }

        // Generate HyperKit configuration
        std::string config = generateHyperKitConfig();
        std::string configPath = vmDir_ + "/config";
        writeFile(configPath, config);

        // Start HyperKit process
        if (!startHyperKitProcess(configPath)) {
            return false;
        }

        // Wait for VM to boot and establish control connection
        if (!waitForControlSocket()) {
            stop();
            return false;
        }

        running_ = true;
        return true;
    }

    bool stop() override {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!running_) return true;

        // Send shutdown command via control socket
        sendControlCommand("shutdown");

        // Wait for HyperKit process to exit
        if (hyperkit_pid_ > 0) {
            int status;
            waitpid(hyperkit_pid_, &status, 0);
            hyperkit_pid_ = -1;
        }

        // Close control socket
        if (control_socket_ >= 0) {
            close(control_socket_);
            control_socket_ = -1;
        }

        running_ = false;
        return true;
    }

    bool pause() override {
        return sendControlCommand("pause");
    }

    bool resume() override {
        return sendControlCommand("resume");
    }

    bool isRunning() const override {
        if (hyperkit_pid_ > 0) {
            int result = kill(hyperkit_pid_, 0);
            return result == 0;
        }
        return false;
    }

    bool setMemory(size_t memoryMB) override {
        config_.defaultMemoryMB = memoryMB;
        // Memory changes require VM restart
        return false;
    }

    bool setCpuCount(size_t cpuCount) override {
        config_.defaultCpuCount = cpuCount;
        // CPU changes require VM restart
        return false;
    }

    bool addDisk(const std::string& imagePath, size_t sizeGB) override {
        std::string diskPath = vmDir_ + "/disk_" + std::to_string(std::hash<std::string>{}(imagePath)) + ".img";

        if (!createDiskImage(diskPath, sizeGB)) {
            return false;
        }

        // Attach disk to running VM (if supported)
        std::string command = "add_disk " + diskPath;
        return sendControlCommand(command);
    }

    bool mountShared(const std::string& hostPath, const std::string& guestPath) override {
        // Use 9p or virtio-fs for file sharing
        std::string shareName = "share_" + std::to_string(std::hash<std::string>{}(hostPath));

        if (config_.enableVirtioFs) {
            return mountVirtioFs(hostPath, guestPath, shareName);
        } else {
            return mount9PShare(hostPath, guestPath, shareName);
        }
    }

    bool addNetworkInterface(const std::string& networkName) override {
        std::string command = "add_network " + networkName;
        return sendControlCommand(command);
    }

    bool configureNetwork(const std::string& ipAddress, const std::string& gateway) override {
        std::string command = "configure_network " + ipAddress + " " + gateway;
        return sendControlCommand(command);
    }

    std::string getIpAddress() const override {
        std::string response;
        if (sendControlCommand("get_ip", response)) {
            return response;
        }
        return "";
    }

    bool executeCommand(const std::string& command, std::string& output) override {
        std::string fullCommand = "exec " + command;
        return sendControlCommand(fullCommand, output);
    }

    bool copyFileToGuest(const std::string& hostPath, const std::string& guestPath) override {
        // Use control socket to transfer file
        std::string command = "copy_to_guest " + hostPath + " " + guestPath;
        return sendControlCommand(command);
    }

    bool copyFileFromGuest(const std::string& guestPath, const std::string& hostPath) override {
        std::string command = "copy_from_guest " + guestPath + " " + hostPath;
        return sendControlCommand(command);
    }

    size_t getMemoryUsage() const override {
        std::string response;
        if (sendControlCommand("get_memory_usage", response)) {
            return std::stoull(response);
        }
        return 0;
    }

    double getCpuUsage() const override {
        std::string response;
        if (sendControlCommand("get_cpu_usage", response)) {
            return std::stod(response);
        }
        return 0.0;
    }

    size_t getDiskUsage() const override {
        std::string response;
        if (sendControlCommand("get_disk_usage", response)) {
            return std::stoull(response);
        }
        return 0;
    }

private:
    bool createDiskImage() {
        std::string diskPath = vmDir_ + "/disk.img";
        return createDiskImage(diskPath, 32); // Default 32GB
    }

    bool createDiskImage(const std::string& path, size_t sizeGB) {
        // Create qcow2 disk image using qemu-img
        std::string command = "qemu-img create -f qcow2 " + path + " " +
                             std::to_string(sizeGB) + "G";

        int result = std::system(command.c_str());
        return result == 0;
    }

    std::string generateHyperKitConfig() {
        // Generate HyperKit configuration file
        return "# HyperKit configuration\n"
               "kernel=" + vmDir_ + "/vmlinuz\n"
               "initrd=" + vmDir_ + "/initrd.img\n"
               "memory=" + std::to_string(config_.defaultMemoryMB) + "\n"
               "cpus=" + std::to_string(config_.defaultCpuCount) + "\n"
               "disk=" + vmDir_ + "/disk.img\n"
               "uuid=" + generateVmUuid() + "\n"
               "network=vmnet\n"
               "console=pty\n"
               "control-socket=" + vmDir_ + "/control.sock\n";
    }

    std::string generateVmUuid() {
        // Generate unique VM UUID
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 15);

        std::string uuid;
        for (int i = 0; i < 32; ++i) {
            uuid += "0123456789abcdef"[dis(gen)];
            if (i == 7 || i == 11 || i == 15 || i == 19) {
                uuid += '-';
            }
        }
        return uuid;
    }

    bool startHyperKitProcess(const std::string& configPath) {
        pid_t pid = fork();
        if (pid == 0) {
            // Child process - start HyperKit
            const char* args[] = {
                "hyperkit",
                "-F", configPath.c_str(),
                nullptr
            };

            execvp("hyperkit", const_cast<char**>(args));
            _exit(1);
        } else if (pid > 0) {
            hyperkit_pid_ = pid;
            return true;
        }

        return false;
    }

    bool waitForControlSocket() {
        std::string socketPath = vmDir_ + "/control.sock";

        // Wait for socket to appear
        for (int i = 0; i < 30; ++i) {
            if (std::filesystem::exists(socketPath)) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        // Connect to control socket
        control_socket_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if (control_socket_ < 0) {
            return false;
        }

        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, socketPath.c_str(), sizeof(addr.sun_path) - 1);

        if (connect(control_socket_, reinterpret_cast<struct sockaddr*>(&addr),
                    sizeof(addr)) < 0) {
            close(control_socket_);
            control_socket_ = -1;
            return false;
        }

        return true;
    }

    bool sendControlCommand(const std::string& command) {
        std::string response;
        return sendControlCommand(command, response);
    }

    bool sendControlCommand(const std::string& command, std::string& response) {
        if (control_socket_ < 0) {
            return false;
        }

        // Send command
        ssize_t sent = send(control_socket_, command.c_str(), command.length(), 0);
        if (sent < 0) {
            return false;
        }

        // Receive response
        char buffer[4096];
        ssize_t received = recv(control_socket_, buffer, sizeof(buffer) - 1, 0);
        if (received < 0) {
            return false;
        }

        buffer[received] = '\0';
        response = buffer;

        return response.substr(0, 2) == "OK";
    }

    bool mountVirtioFs(const std::string& hostPath, const std::string& guestPath,
                      const std::string& shareName) {
        // Mount using virtio-fs (requires kernel support)
        std::string command = "mount_virtiofs " + hostPath + " " + guestPath + " " + shareName;
        return sendControlCommand(command);
    }

    bool mount9PShare(const std::string& hostPath, const std::string& guestPath,
                     const std::string& shareName) {
        // Mount using 9p filesystem
        std::string command = "mount_9p " + hostPath + " " + guestPath + " " + shareName;
        return sendControlCommand(command);
    }

    bool writeFile(const std::string& path, const std::string& content) {
        std::ofstream file(path);
        if (file.is_open()) {
            file << content;
            file.close();
            return true;
        }
        return false;
    }
};

// macOS platform factory
class MacOSPlatformFactory : public IPlatformFactory {
private:
    PlatformConfig config_;
    bool initialized_ = false;

public:
    MacOSPlatformFactory() {
        config_.platform = Platform::MACOS;
        config_.backend = VirtualizationBackend::HYPER_KIT;
        config_.defaultSharePath = "/Users/Shared/Docker/VMS";
        config_.defaultMemoryMB = 2048;
        config_.defaultCpuCount = 2;
        config_.enableVirtioFs = true; // Enable virtio-fs on macOS
        config_.networkMode = "nat";
    }

    bool initialize() override {
        if (initialized_) return true;

        // Check HyperKit availability
        if (!checkHyperKitAvailability()) {
            // Fallback to QEMU
            config_.backend = VirtualizationBackend::QEMU;
            if (!checkQemuAvailability()) {
                return false;
            }
        }

        // Create default directories
        std::filesystem::create_directories(config_.defaultSharePath);

        initialized_ = true;
        return true;
    }

    void cleanup() override {
        if (initialized_) {
            // Cleanup resources
            initialized_ = false;
        }
    }

    std::unique_ptr<IVirtualMachine> createVirtualMachine(
        const PlatformConfig& config) override {
        if (!initialized_) return nullptr;

        // Merge with factory config
        PlatformConfig vmConfig = config_;
        vmConfig.defaultMemoryMB = config.defaultMemoryMB > 0 ?
                                 config.defaultMemoryMB : vmConfig.defaultMemoryMB;
        vmConfig.defaultCpuCount = config.defaultCpuCount > 0 ?
                                config.defaultCpuCount : vmConfig.defaultCpuCount;

        if (vmConfig.backend == VirtualizationBackend::HYPER_KIT) {
            return std::make_unique<HyperKitVirtualMachine>("docker-vm", vmConfig);
        } else {
            // Fallback to QEMU implementation
            return createQemuVirtualMachine(vmConfig);
        }
    }

    std::unique_ptr<IFileSystemBridge> createFileSystemBridge(
        const PlatformConfig& config) override {
        // Create virtio-fs or 9p-based file system bridge
        return nullptr; // Implementation would go here
    }

    std::unique_ptr<INetworkProxy> createNetworkProxy(
        const PlatformConfig& config) override {
        // Create vmnet-based network proxy
        return nullptr; // Implementation would go here
    }

    Platform getPlatform() const override {
        return Platform::MACOS;
    }

    VirtualizationBackend getBackend() const override {
        return config_.backend;
    }

private:
    bool checkHyperKitAvailability() {
        // Check if HyperKit is available
        int result = std::system("which hyperkit > /dev/null 2>&1");
        return result == 0;
    }

    bool checkQemuAvailability() {
        // Check if QEMU is available
        int result = std::system("which qemu-system-x86_64 > /dev/null 2>&1");
        return result == 0;
    }

    std::unique_ptr<IVirtualMachine> createQemuVirtualMachine(const PlatformConfig& config) {
        // Create QEMU-based VM implementation
        return nullptr; // Implementation would go here
    }
};

} // namespace cross_platform
```

## Linux VM Management

### Lightweight VM Orchestrator

```cpp
#include <sys/wait.h>
#include <sys/resource.h>
#include <signal.h>

namespace cross_platform {

// Linux VM pool for efficient resource utilization
class LinuxVmPool {
private:
    struct VmInstance {
        std::string id;
        std::unique_ptr<IVirtualMachine> vm;
        std::atomic<bool> in_use{false};
        std::chrono::steady_clock::time_point last_used;
        std::string assigned_container;
    };

    std::vector<std::unique_ptr<VmInstance>> vm_pool_;
    std::queue<std::string> available_vms_;
    std::mutex pool_mutex_;
    std::condition_variable pool_cv_;

    PlatformConfig config_;
    std::unique_ptr<IPlatformFactory> factory_;
    size_t max_pool_size_ = 5;
    size_t min_pool_size_ = 1;

    // Background management thread
    std::thread management_thread_;
    std::atomic<bool> running_{true};

public:
    LinuxVmPool(const PlatformConfig& config) : config_(config) {
        factory_ = createPlatformFactory(config.platform);
        factory_->initialize();

        management_thread_ = std::thread(&LinuxVmPool::managePool, this);

        // Initialize minimum pool size
        for (size_t i = 0; i < min_pool_size_; ++i) {
            createVmInstance();
        }
    }

    ~LinuxVmPool() {
        running_ = false;
        if (management_thread_.joinable()) {
            management_thread_.join();
        }

        // Shutdown all VMs
        std::lock_guard<std::mutex> lock(pool_mutex_);
        for (auto& instance : vm_pool_) {
            if (instance->vm) {
                instance->vm->stop();
            }
        }

        factory_->cleanup();
    }

    std::string acquireVm(const std::string& containerId) {
        std::unique_lock<std::mutex> lock(pool_mutex_);

        // Wait for available VM or create new one
        pool_cv_.wait(lock, [this] {
            return !available_vms_.empty() || vm_pool_.size() < max_pool_size_;
        });

        std::string vmId;

        if (!available_vms_.empty()) {
            vmId = available_vms_.front();
            available_vms_.pop();
        } else {
            // Create new VM instance
            vmId = createVmInstance();
        }

        if (!vmId.empty()) {
            auto instance = findVmInstance(vmId);
            if (instance) {
                instance->in_use = true;
                instance->assigned_container = containerId;
                instance->last_used = std::chrono::steady_clock::now();

                // Ensure VM is running
                if (!instance->vm->isRunning()) {
                    instance->vm->start();
                }
            }
        }

        return vmId;
    }

    void releaseVm(const std::string& vmId) {
        std::lock_guard<std::mutex> lock(pool_mutex_);

        auto instance = findVmInstance(vmId);
        if (instance) {
            instance->in_use = false;
            instance->assigned_container.clear();
            instance->last_used = std::chrono::steady_clock::now();
            available_vms_.push(vmId);
        }

        pool_cv_.notify_one();
    }

    IVirtualMachine* getVm(const std::string& vmId) {
        std::lock_guard<std::mutex> lock(pool_mutex_);

        auto instance = findVmInstance(vmId);
        return instance ? instance->vm.get() : nullptr;
    }

    std::vector<std::string> getVmIds() const {
        std::lock_guard<std::mutex> lock(pool_mutex_);

        std::vector<std::string> ids;
        for (const auto& instance : vm_pool_) {
            ids.push_back(instance->id);
        }
        return ids;
    }

private:
    std::string createVmInstance() {
        auto vm = factory_->createVirtualMachine(config_);
        if (!vm) {
            return "";
        }

        auto instance = std::make_unique<VmInstance>();
        instance->id = generateVmId();
        instance->vm = std::move(vm);
        instance->last_used = std::chrono::steady_clock::now();

        // Start the VM
        if (instance->vm->start()) {
            std::string vmId = instance->id;
            vm_pool_.push_back(std::move(instance));
            available_vms_.push(vmId);
            return vmId;
        }

        return "";
    }

    VmInstance* findVmInstance(const std::string& vmId) {
        auto it = std::find_if(vm_pool_.begin(), vm_pool_.end(),
            [&vmId](const std::unique_ptr<VmInstance>& instance) {
                return instance->id == vmId;
            });
        return it != vm_pool_.end() ? it->get() : nullptr;
    }

    std::string generateVmId() {
        static std::atomic<uint64_t> counter{0};
        return "vm-" + std::to_string(counter++);
    }

    void managePool() {
        while (running_) {
            std::this_thread::sleep_for(std::chrono::minutes(5));

            std::lock_guard<std::mutex> lock(pool_mutex_);

            // Cleanup idle VMs
            auto now = std::chrono::steady_clock::now();
            auto idleThreshold = std::chrono::minutes(30);

            for (auto it = vm_pool_.begin(); it != vm_pool_.end();) {
                if (!(*it)->in_use &&
                    (now - (*it)->last_used) > idleThreshold &&
                    vm_pool_.size() > min_pool_size_) {

                    // Remove from available queue
                    std::queue<std::string> new_queue;
                    while (!available_vms_.empty()) {
                        if (available_vms_.front() != (*it)->id) {
                            new_queue.push(available_vms_.front());
                        }
                        available_vms_.pop();
                    }
                    available_vms_ = std::move(new_queue);

                    // Stop and remove VM
                    (*it)->vm->stop();
                    it = vm_pool_.erase(it);
                } else {
                    ++it;
                }
            }

            // Ensure minimum pool size
            while (vm_pool_.size() < min_pool_size_) {
                createVmInstance();
            }
        }
    }

    std::unique_ptr<IPlatformFactory> createPlatformFactory(Platform platform) {
        switch (platform) {
            case Platform::WINDOWS:
                return std::make_unique<WindowsPlatformFactory>();
            case Platform::MACOS:
                return std::make_unique<MacOSPlatformFactory>();
            case Platform::LINUX:
                // Return null for Linux (native containers)
                return nullptr;
            default:
                return nullptr;
        }
    }
};

} // namespace cross_platform
```

## File Sharing and Performance

### Cross-Platform File System Bridge

```cpp
namespace cross_platform {

// High-performance file system bridge
class FileSystemBridge : public IFileSystemBridge {
private:
    PlatformConfig config_;
    std::unique_ptr<IVirtualMachine> vm_;
    std::unordered_map<std::string, std::string> volume_mappings_;
    mutable std::shared_mutex mappings_mutex_;

    // Performance optimization
    std::unique_ptr<ThreadPool> io_thread_pool_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> cache_timestamps_;
    std::mutex cache_mutex_;
    bool caching_enabled_ = true;
    bool async_enabled_ = true;

public:
    FileSystemBridge(const PlatformConfig& config,
                    std::unique_ptr<IVirtualMachine> vm)
        : config_(config), vm_(std::move(vm)) {

        io_thread_pool_ = std::make_unique<ThreadPool>(config_.ioThreadCount);
    }

    bool createSharedVolume(const std::string& volumeName,
                           const std::string& hostPath) override {
        std::lock_guard<std::shared_mutex> lock(mappings_mutex_);

        // Create host directory if it doesn't exist
        std::filesystem::create_directories(hostPath);

        // Mount in guest VM
        std::string guestPath = "/mnt/volumes/" + volumeName;
        if (vm_->mountShared(hostPath, guestPath)) {
            volume_mappings_[volumeName] = hostPath;
            return true;
        }

        return false;
    }

    bool deleteSharedVolume(const std::string& volumeName) override {
        std::lock_guard<std::shared_mutex> lock(mappings_mutex_);

        auto it = volume_mappings_.find(volumeName);
        if (it == volume_mappings_.end()) {
            return false;
        }

        std::string guestPath = "/mnt/volumes/" + volumeName;
        vm_->unmountVolume(guestPath);

        volume_mappings_.erase(it);
        return true;
    }

    bool mountVolume(const std::string& volumeName,
                    const std::string& mountPoint) override {
        std::shared_lock<std::shared_mutex> lock(mappings_mutex_);

        auto it = volume_mappings_.find(volumeName);
        if (it == volume_mappings_.end()) {
            return false;
        }

        std::string command = "mount --bind " + it->second + " " + mountPoint;
        std::string output;
        return vm_->executeCommand(command, output);
    }

    bool unmountVolume(const std::string& mountPoint) override {
        std::string command = "umount " + mountPoint;
        std::string output;
        return vm_->executeCommand(command, output);
    }

    bool copyFile(const std::string& source, const std::string& destination) override {
        if (async_enabled_) {
            // Submit async task
            auto future = io_thread_pool_->submit([this, source, destination]() {
                return copyFileSync(source, destination);
            });

            return future.get();
        } else {
            return copyFileSync(source, destination);
        }
    }

    bool syncDirectory(const std::string& source, const std::string& destination) override {
        if (async_enabled_) {
            auto future = io_thread_pool_->submit([this, source, destination]() {
                return syncDirectorySync(source, destination);
            });

            return future.get();
        } else {
            return syncDirectorySync(source, destination);
        }
    }

    std::vector<std::string> listFiles(const std::string& path) override {
        if (caching_enabled_) {
            auto cached = getCachedListing(path);
            if (cached) {
                return *cached;
            }
        }

        std::vector<std::string> files;
        std::string command = "find " + path + " -type f -printf '%f\\n'";
        std::string output;

        if (vm_->executeCommand(command, output)) {
            std::istringstream iss(output);
            std::string line;
            while (std::getline(iss, line)) {
                if (!line.empty()) {
                    files.push_back(line);
                }
            }
        }

        if (caching_enabled_) {
            cacheListing(path, files);
        }

        return files;
    }

    bool enableCaching(bool enabled) override {
        caching_enabled_ = enabled;
        if (!enabled) {
            clearCache();
        }
        return true;
    }

    bool enableAsyncOperations(bool enabled) override {
        async_enabled_ = enabled;
        return true;
    }

    void flushCache() override {
        clearCache();
    }

private:
    bool copyFileSync(const std::string& source, const std::string& destination) {
        // Determine if source is on host or guest
        if (isHostPath(source) && isGuestPath(destination)) {
            return vm_->copyFileToGuest(source, destination);
        } else if (isGuestPath(source) && isHostPath(destination)) {
            return vm_->copyFileFromGuest(source, destination);
        } else {
            // Both paths on same side, use local copy
            std::string command = "cp -r " + source + " " + destination;
            std::string output;
            return vm_->executeCommand(command, output);
        }
    }

    bool syncDirectorySync(const std::string& source, const std::string& destination) {
        // Use rsync for efficient synchronization
        std::string command = "rsync -av --delete " + source + "/ " + destination + "/";
        std::string output;
        return vm_->executeCommand(command, output);
    }

    bool isHostPath(const std::string& path) {
        return path.find(config_.defaultSharePath) == 0;
    }

    bool isGuestPath(const std::string& path) {
        return !isHostPath(path);
    }

    void cacheListing(const std::string& path, const std::vector<std::string>& files) {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        cache_timestamps_[path] = std::chrono::steady_clock::now();
        // Store files in cache (implementation would use proper cache storage)
    }

    std::optional<std::vector<std::string>> getCachedListing(const std::string& path) {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        auto it = cache_timestamps_.find(path);
        if (it != cache_timestamps_.end()) {
            auto now = std::chrono::steady_clock::now();
            if (now - it->second < std::chrono::seconds(30)) {
                // Return cached result (implementation would retrieve from cache)
                return std::vector<std::string>{};
            }
        }
        return std::nullopt;
    }

    void clearCache() {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        cache_timestamps_.clear();
    }
};

// Thread pool for async I/O operations
class ThreadPool {
private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex queue_mutex_;
    std::condition_variable condition_;
    bool stop_;

public:
    explicit ThreadPool(size_t threads) : stop_(false) {
        for (size_t i = 0; i < threads; ++i) {
            workers_.emplace_back([this] {
                while (true) {
                    std::function<void()> task;

                    {
                        std::unique_lock<std::mutex> lock(queue_mutex_);
                        condition_.wait(lock, [this] {
                            return stop_ || !tasks_.empty();
                        });

                        if (stop_ && tasks_.empty()) {
                            return;
                        }

                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }

                    task();
                }
            });
        }
    }

    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<typename std::result_of<F(Args...)>::type> {

        using return_type = typename std::result_of<F(Args...)>::type;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<return_type> result = task->get_future();

        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            if (stop_) {
                throw std::runtime_error("submit on stopped ThreadPool");
            }

            tasks_.emplace([task](){ (*task)(); });
        }

        condition_.notify_one();
        return result;
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            stop_ = true;
        }

        condition_.notify_all();

        for (std::thread& worker : workers_) {
            worker.join();
        }
    }
};

} // namespace cross_platform
```

## Network Virtualization

### Cross-Platform Network Proxy

```cpp
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

namespace cross_platform {

// Network proxy for container networking
class NetworkProxy : public INetworkProxy {
private:
    PlatformConfig config_;
    std::unique_ptr<IVirtualMachine> vm_;
    std::unordered_map<std::string, std::vector<uint16_t>> port_mappings_;
    std::unordered_map<std::string, std::string> container_networks_;
    std::mutex networks_mutex_;

    // NAT table for port forwarding
    struct NatEntry {
        std::string containerId;
        uint16_t hostPort;
        uint16_t containerPort;
        std::string protocol; // "tcp" or "udp"
        std::thread forward_thread;
        std::atomic<bool> active{true};
    };

    std::vector<std::unique_ptr<NatEntry>> nat_entries_;
    std::mutex nat_mutex_;

public:
    NetworkProxy(const PlatformConfig& config, std::unique_ptr<IVirtualMachine> vm)
        : config_(config), vm_(std::move(vm)) {}

    bool createNetwork(const std::string& networkName,
                      const std::unordered_map<std::string, std::string>& options) override {
        std::lock_guard<std::mutex> lock(networks_mutex_);

        std::string subnet = options.count("subnet") ? options.at("subnet") : "172.18.0.0/16";
        std::string gateway = options.count("gateway") ? options.at("gateway") : "172.18.0.1";

        // Create bridge network in VM
        std::string command = "docker network create --driver bridge --subnet=" + subnet +
                             " --gateway=" + gateway + " " + networkName;
        std::string output;

        if (vm_->executeCommand(command, output)) {
            // Configure host-side routing if needed
            if (config_.networkMode == "nat") {
                configureNatRouting(networkName, subnet, gateway);
            }
            return true;
        }

        return false;
    }

    bool deleteNetwork(const std::string& networkName) override {
        std::lock_guard<std::mutex> lock(networks_mutex_);

        std::string command = "docker network rm " + networkName;
        std::string output;
        return vm_->executeCommand(command, output);
    }

    bool connectContainer(const std::string& networkId, const std::string& containerId) override {
        std::lock_guard<std::mutex> lock(networks_mutex_);

        std::string command = "docker network connect " + networkId + " " + containerId;
        std::string output;

        if (vm_->executeCommand(command, output)) {
            container_networks_[containerId] = networkId;
            return true;
        }

        return false;
    }

    bool disconnectContainer(const std::string& networkId, const std::string& containerId) override {
        std::lock_guard<std::mutex> lock(networks_mutex_);

        std::string command = "docker network disconnect " + networkId + " " + containerId;
        std::string output;

        if (vm_->executeCommand(command, output)) {
            container_networks_.erase(containerId);

            // Clean up port mappings for this container
            cleanupPortMappings(containerId);
            return true;
        }

        return false;
    }

    bool exposePort(const std::string& containerId, uint16_t containerPort, uint16_t hostPort) override {
        std::lock_guard<std::mutex> lock(nat_mutex_);

        // Find available host port if not specified
        if (hostPort == 0) {
            hostPort = findAvailablePort();
            if (hostPort == 0) {
                return false;
            }
        }

        // Create NAT entry
        auto natEntry = std::make_unique<NatEntry>();
        natEntry->containerId = containerId;
        natEntry->hostPort = hostPort;
        natEntry->containerPort = containerPort;
        natEntry->protocol = "tcp";

        // Start port forwarding thread
        natEntry->forward_thread = std::thread([this, natEntry = natEntry.get()]() {
            portForwardWorker(natEntry);
        });

        port_mappings_[containerId].push_back(hostPort);
        nat_entries_.push_back(std::move(natEntry));

        return true;
    }

    bool hidePort(const std::string& containerId, uint16_t hostPort) override {
        std::lock_guard<std::mutex> lock(nat_mutex_);

        // Find and remove NAT entry
        auto it = std::find_if(nat_entries_.begin(), nat_entries_.end(),
            [&](const std::unique_ptr<NatEntry>& entry) {
                return entry->containerId == containerId && entry->hostPort == hostPort;
            });

        if (it != nat_entries_.end()) {
            (*it)->active = false;
            (*it)->forward_thread.join();
            nat_entries_.erase(it);

            // Remove from port mappings
            auto& ports = port_mappings_[containerId];
            ports.erase(std::remove(ports.begin(), ports.end(), hostPort), ports.end());

            return true;
        }

        return false;
    }

    bool addDnsEntry(const std::string& containerId, const std::string& hostname,
                    const std::string& ipAddress) override {
        // Add DNS entry to host resolver or configure container DNS
        std::string command = "echo '" + ipAddress + " " + hostname + "' >> /etc/hosts";
        std::string output;
        return vm_->executeCommand(command, output);
    }

    bool removeDnsEntry(const std::string& containerId, const std::string& hostname) override {
        // Remove DNS entry
        std::string command = "sed -i '/" + hostname + "/d' /etc/hosts";
        std::string output;
        return vm_->executeCommand(command, output);
    }

private:
    void configureNatRouting(const std::string& networkName, const std::string& subnet,
                           const std::string& gateway) {
        // Configure NAT routing based on platform
        switch (config_.platform) {
            case Platform::WINDOWS:
                configureWindowsNat(networkName, subnet, gateway);
                break;
            case Platform::MACOS:
                configureMacOSNat(networkName, subnet, gateway);
                break;
            case Platform::LINUX:
                configureLinuxNat(networkName, subnet, gateway);
                break;
        }
    }

    void configureWindowsNat(const std::string& networkName, const std::string& subnet,
                           const std::string& gateway) {
        // Configure Windows NAT using netsh
        std::string command = "netsh interface portproxy add v4tov4 " +
                             "listenport=0 listenaddress=0.0.0.0 " +
                             "connectport=0 connectaddress=" + gateway;
        std::system(command.c_str());
    }

    void configureMacOSNat(const std::string& networkName, const std::string& subnet,
                          const std::string& gateway) {
        // Configure macOS NAT using pfctl
        std::string command = "echo 'nat on en0 from " + subnet + " to any -> (en0)' | " +
                             "sudo pfctl -f -";
        std::system(command.c_str());
    }

    void configureLinuxNat(const std::string& networkName, const std::string& subnet,
                          const std::string& gateway) {
        // Configure Linux NAT using iptables
        std::string command = "sudo iptables -t nat -A POSTROUTING -s " + subnet +
                             " -o eth0 -j MASQUERADE";
        std::system(command.c_str());
    }

    uint16_t findAvailablePort() {
        // Find available port on host
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) return 0;

        sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = 0; // Let OS choose port

        if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
            socklen_t len = sizeof(addr);
            if (getsockname(sock, reinterpret_cast<sockaddr*>(&addr), &len) == 0) {
                close(sock);
                return ntohs(addr.sin_port);
            }
        }

        close(sock);
        return 0;
    }

    void portForwardWorker(NatEntry* entry) {
        // TCP port forwarding worker
        int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (serverSocket < 0) return;

        sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = INADDR_ANY;
        serverAddr.sin_port = htons(entry->hostPort);

        if (bind(serverSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) < 0) {
            close(serverSocket);
            return;
        }

        if (listen(serverSocket, 10) < 0) {
            close(serverSocket);
            return;
        }

        while (entry->active) {
            sockaddr_in clientAddr;
            socklen_t clientLen = sizeof(clientAddr);

            int clientSocket = accept(serverSocket, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
            if (clientSocket < 0) continue;

            // Handle connection in thread
            std::thread([this, entry, clientSocket]() {
                handleTcpForward(entry, clientSocket);
            }).detach();
        }

        close(serverSocket);
    }

    void handleTcpForward(NatEntry* entry, int clientSocket) {
        // Connect to container port
        std::string containerIp = getContainerIp(entry->containerId);
        if (containerIp.empty()) {
            close(clientSocket);
            return;
        }

        int containerSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (containerSocket < 0) {
            close(clientSocket);
            return;
        }

        sockaddr_in containerAddr;
        containerAddr.sin_family = AF_INET;
        containerAddr.sin_port = htons(entry->containerPort);
        inet_pton(AF_INET, containerIp.c_str(), &containerAddr.sin_addr);

        if (connect(containerSocket, reinterpret_cast<sockaddr*>(&containerAddr), sizeof(containerAddr)) < 0) {
            close(clientSocket);
            close(containerSocket);
            return;
        }

        // Forward data between client and container
        fd_set readFds;
        char buffer[4096];

        while (entry->active) {
            FD_ZERO(&readFds);
            FD_SET(clientSocket, &readFds);
            FD_SET(containerSocket, &readFds);

            int maxFd = std::max(clientSocket, containerSocket);
            int activity = select(maxFd + 1, &readFds, nullptr, nullptr, nullptr);

            if (activity < 0) break;

            if (FD_ISSET(clientSocket, &readFds)) {
                ssize_t bytesRead = recv(clientSocket, buffer, sizeof(buffer), 0);
                if (bytesRead <= 0) break;

                send(containerSocket, buffer, bytesRead, 0);
            }

            if (FD_ISSET(containerSocket, &readFds)) {
                ssize_t bytesRead = recv(containerSocket, buffer, sizeof(buffer), 0);
                if (bytesRead <= 0) break;

                send(clientSocket, buffer, bytesRead, 0);
            }
        }

        close(clientSocket);
        close(containerSocket);
    }

    std::string getContainerIp(const std::string& containerId) {
        std::string command = "docker inspect -f '{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}' " + containerId;
        std::string output;

        if (vm_->executeCommand(command, output)) {
            // Remove quotes and whitespace
            output.erase(std::remove(output.begin(), output.end(), '\''), output.end());
            output.erase(std::remove(output.begin(), output.end(), '"'), output.end());
            output.erase(std::remove_if(output.begin(), output.end(), ::isspace), output.end());
            return output;
        }

        return "";
    }

    void cleanupPortMappings(const std::string& containerId) {
        std::lock_guard<std::mutex> lock(nat_mutex_);

        // Remove all NAT entries for this container
        nat_entries_.erase(
            std::remove_if(nat_entries_.begin(), nat_entries_.end(),
                [&](const std::unique_ptr<NatEntry>& entry) {
                    if (entry->containerId == containerId) {
                        entry->active = false;
                        entry->forward_thread.join();
                        return true;
                    }
                    return false;
                }),
            nat_entries_.end()
        );

        port_mappings_.erase(containerId);
    }
};

} // namespace cross_platform
```

## Complete Implementation

### Cross-Platform Container Runtime

```cpp
namespace cross_platform {

// Main cross-platform container runtime
class CrossPlatformContainerRuntime {
private:
    PlatformConfig config_;
    std::unique_ptr<IPlatformFactory> factory_;
    std::unique_ptr<LinuxVmPool> vm_pool_;
    std::unique_ptr<IFileSystemBridge> fs_bridge_;
    std::unique_ptr<INetworkProxy> network_proxy_;

    // Container management
    struct ContainerInfo {
        std::string id;
        std::string vm_id;
        std::string image;
        std::unordered_map<std::string, std::string> config;
        std::chrono::steady_clock::time_point created_at;
    };

    std::unordered_map<std::string, ContainerInfo> containers_;
    mutable std::shared_mutex containers_mutex_;

public:
    CrossPlatformContainerRuntime(const PlatformConfig& config) : config_(config) {
        factory_ = createPlatformFactory();
        if (!factory_->initialize()) {
            throw std::runtime_error("Failed to initialize platform factory");
        }

        // Initialize VM pool for non-Linux platforms
        if (config_.platform != Platform::LINUX) {
            vm_pool_ = std::make_unique<LinuxVmPool>(config_);
        }

        initialized_ = true;
    }

    ~CrossPlatformContainerRuntime() {
        cleanup();
    }

    std::string createContainer(const std::string& image,
                               const std::unordered_map<std::string, std::string>& options) {
        if (config_.platform != Platform::LINUX) {
            return createContainerInVm(image, options);
        } else {
            return createNativeContainer(image, options);
        }
    }

    bool startContainer(const std::string& containerId) {
        std::shared_lock<std::shared_mutex> lock(containers_mutex_);

        auto it = containers_.find(containerId);
        if (it == containers_.end()) {
            return false;
        }

        if (config_.platform != Platform::LINUX) {
            return startContainerInVm(it->second);
        } else {
            return startNativeContainer(it->second);
        }
    }

    bool stopContainer(const std::string& containerId) {
        std::shared_lock<std::shared_mutex> lock(containers_mutex_);

        auto it = containers_.find(containerId);
        if (it == containers_.end()) {
            return false;
        }

        if (config_.platform != Platform::LINUX) {
            return stopContainerInVm(it->second);
        } else {
            return stopNativeContainer(it->second);
        }
    }

    bool removeContainer(const std::string& containerId) {
        std::unique_lock<std::shared_mutex> lock(containers_mutex_);

        auto it = containers_.find(containerId);
        if (it == containers_.end()) {
            return false;
        }

        bool result = false;
        if (config_.platform != Platform::LINUX) {
            result = removeContainerInVm(it->second);
            if (result && vm_pool_) {
                vm_pool_->releaseVm(it->second.vm_id);
            }
        } else {
            result = removeNativeContainer(it->second);
        }

        if (result) {
            containers_.erase(it);
        }

        return result;
    }

    std::vector<std::string> listContainers() const {
        std::shared_lock<std::shared_mutex> lock(containers_mutex_);

        std::vector<std::string> ids;
        for (const auto& [id, info] : containers_) {
            ids.push_back(id);
        }
        return ids;
    }

    std::unordered_map<std::string, std::string> getContainerInfo(const std::string& containerId) const {
        std::shared_lock<std::shared_mutex> lock(containers_mutex_);

        auto it = containers_.find(containerId);
        if (it != containers_.end()) {
            return it->second.config;
        }
        return {};
    }

private:
    bool initialized_ = false;

    std::string createContainerInVm(const std::string& image,
                                   const std::unordered_map<std::string, std::string>& options) {
        if (!vm_pool_) {
            return "";
        }

        // Acquire VM from pool
        std::string vmId = vm_pool_->acquireVm(generateContainerId());
        if (vmId.empty()) {
            return "";
        }

        auto vm = vm_pool_->getVm(vmId);
        if (!vm) {
            vm_pool_->releaseVm(vmId);
            return "";
        }

        // Create container in VM
        std::string command = buildDockerCreateCommand(image, options);
        std::string output;

        if (vm->executeCommand(command, output)) {
            std::string containerId = parseContainerId(output);
            if (!containerId.empty()) {
                // Register container
                std::unique_lock<std::shared_mutex> lock(containers_mutex_);

                ContainerInfo info;
                info.id = containerId;
                info.vm_id = vmId;
                info.image = image;
                info.config = options;
                info.created_at = std::chrono::steady_clock::now();

                containers_[containerId] = info;
                return containerId;
            }
        }

        vm_pool_->releaseVm(vmId);
        return "";
    }

    std::string createNativeContainer(const std::string& image,
                                     const std::unordered_map<std::string, std::string>& options) {
        // Create container using native Linux runtime
        std::string command = buildDockerCreateCommand(image, options);
        std::string output = executeCommand(command);

        return parseContainerId(output);
    }

    bool startContainerInVm(const ContainerInfo& info) {
        auto vm = vm_pool_->getVm(info.vm_id);
        if (!vm) {
            return false;
        }

        std::string command = "docker start " + info.id;
        std::string output;
        return vm->executeCommand(command, output);
    }

    bool startNativeContainer(const ContainerInfo& info) {
        std::string command = "docker start " + info.id;
        std::string output = executeCommand(command);
        return output.empty(); // Docker start returns empty on success
    }

    bool stopContainerInVm(const ContainerInfo& info) {
        auto vm = vm_pool_->getVm(info.vm_id);
        if (!vm) {
            return false;
        }

        std::string command = "docker stop " + info.id;
        std::string output;
        return vm->executeCommand(command, output);
    }

    bool stopNativeContainer(const ContainerInfo& info) {
        std::string command = "docker stop " + info.id;
        std::string output = executeCommand(command);
        return output.empty();
    }

    bool removeContainerInVm(const ContainerInfo& info) {
        auto vm = vm_pool_->getVm(info.vm_id);
        if (!vm) {
            return false;
        }

        std::string command = "docker rm " + info.id;
        std::string output;
        return vm->executeCommand(command, output);
    }

    bool removeNativeContainer(const ContainerInfo& info) {
        std::string command = "docker rm " + info.id;
        std::string output = executeCommand(command);
        return output.empty();
    }

    std::string buildDockerCreateCommand(const std::string& image,
                                        const std::unordered_map<std::string, std::string>& options) {
        std::string command = "docker create";

        for (const auto& [key, value] : options) {
            if (key == "name") {
                command += " --name " + value;
            } else if (key == "port") {
                command += " -p " + value;
            } else if (key == "volume") {
                command += " -v " + value;
            } else if (key == "env") {
                command += " -e " + value;
            } else if (key == "network") {
                command += " --network " + value;
            }
        }

        command += " " + image;
        return command;
    }

    std::string parseContainerId(const std::string& dockerOutput) {
        // Parse container ID from docker create output
        std::istringstream iss(dockerOutput);
        std::string token;
        if (iss >> token) {
            return token;
        }
        return "";
    }

    std::string generateContainerId() {
        static std::atomic<uint64_t> counter{0};
        return "container-" + std::to_string(counter++);
    }

    std::string executeCommand(const std::string& command) {
        std::array<char, 128> buffer;
        std::string result;

        std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
        if (!pipe) {
            return "";
        }

        while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
            result += buffer.data();
        }

        return result;
    }

    void cleanup() {
        if (!initialized_) return;

        // Stop all containers
        std::unique_lock<std::shared_mutex> lock(containers_mutex_);
        for (auto& [id, info] : containers_) {
            if (config_.platform != Platform::LINUX && vm_pool_) {
                stopContainerInVm(info);
                vm_pool_->releaseVm(info.vm_id);
            } else {
                stopNativeContainer(info);
            }
        }
        containers_.clear();

        // Cleanup resources
        if (factory_) {
            factory_->cleanup();
        }
    }

    std::unique_ptr<IPlatformFactory> createPlatformFactory() {
        switch (config_.platform) {
            case Platform::WINDOWS:
                return std::make_unique<WindowsPlatformFactory>();
            case Platform::MACOS:
                return std::make_unique<MacOSPlatformFactory>();
            case Platform::LINUX:
                return nullptr; // Native containers
            default:
                throw std::runtime_error("Unsupported platform");
        }
    }
};

} // namespace cross_platform
```

## Usage Examples

### Cross-Platform Container Runtime Usage

```cpp
#include "cross_platform_runtime.h"

int main() {
    // Detect current platform and configure accordingly
    cross_platform::Platform platform = cross_platform::getCurrentPlatform();

    cross_platform::PlatformConfig config;
    config.platform = platform;
    config.defaultMemoryMB = 4096;
    config.defaultCpuCount = 4;
    config.defaultSharePath = "/tmp/docker-cross-platform";
    config.enableVirtioFs = (platform == cross_platform::Platform::MACOS);
    config.enable9pSharing = true;

    try {
        // Initialize cross-platform runtime
        cross_platform::CrossPlatformContainerRuntime runtime(config);

        // Create container
        std::unordered_map<std::string, std::string> options = {
            {"name", "my-cross-platform-container"},
            {"port", "8080:80"},
            {"volume", "/host/data:/container/data"},
            {"env", "ENV=production"}
        };

        std::string containerId = runtime.createContainer("nginx:alpine", options);
        if (!containerId.empty()) {
            std::cout << "Created container: " << containerId << std::endl;

            // Start container
            if (runtime.startContainer(containerId)) {
                std::cout << "Container started successfully" << std::endl;

                // List containers
                auto containers = runtime.listContainers();
                std::cout << "Running containers: ";
                for (const auto& id : containers) {
                    std::cout << id << " ";
                }
                std::cout << std::endl;

                // Wait for user input
                std::cout << "Press Enter to stop and remove container..." << std::endl;
                std::cin.get();

                // Stop and remove container
                runtime.stopContainer(containerId);
                runtime.removeContainer(containerId);
                std::cout << "Container stopped and removed" << std::endl;
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
```

## Performance Optimizations

### Optimized Cross-Platform Operations

```cpp
namespace cross_platform {

// Performance-optimized file operations
class OptimizedFileSystemBridge : public FileSystemBridge {
private:
    // Pre-allocated buffer pools
    std::vector<std::unique_ptr<char[]>> buffer_pool_;
    std::mutex buffer_pool_mutex_;
    static constexpr size_t BUFFER_SIZE = 1024 * 1024; // 1MB buffers
    static constexpr size_t POOL_SIZE = 10;

public:
    OptimizedFileSystemBridge(const PlatformConfig& config,
                             std::unique_ptr<IVirtualMachine> vm)
        : FileSystemBridge(config, std::move(vm)) {

        // Pre-allocate buffers
        for (size_t i = 0; i < POOL_SIZE; ++i) {
            buffer_pool_.push_back(std::make_unique<char[]>(BUFFER_SIZE));
        }
    }

    bool copyLargeFile(const std::string& source, const std::string& destination) override {
        // Get buffer from pool
        char* buffer = getBuffer();
        if (!buffer) return false;

        try {
            // Use buffered I/O for large files
            std::ifstream src(source, std::ios::binary);
            std::ofstream dst(destination, std::ios::binary);

            if (!src.is_open() || !dst.is_open()) {
                returnBuffer(buffer);
                return false;
            }

            while (!src.eof()) {
                src.read(buffer, BUFFER_SIZE);
                std::streamsize bytesRead = src.gcount();

                if (bytesRead > 0) {
                    dst.write(buffer, bytesRead);
                }
            }

            returnBuffer(buffer);
            return true;

        } catch (const std::exception&) {
            returnBuffer(buffer);
            return false;
        }
    }

    bool syncDirectoryIncremental(const std::string& source, const std::string& destination) override {
        // Use rsync with incremental optimization
        std::string command = "rsync -av --partial --progress --inplace " +
                             source + "/ " + destination + "/";
        std::string output;
        return vm_->executeCommand(command, output);
    }

private:
    char* getBuffer() {
        std::lock_guard<std::mutex> lock(buffer_pool_mutex_);
        if (!buffer_pool_.empty()) {
            auto buffer = std::move(buffer_pool_.back());
            buffer_pool_.pop_back();
            char* ptr = buffer.release();
            return ptr;
        }
        return new char[BUFFER_SIZE];
    }

    void returnBuffer(char* buffer) {
        std::lock_guard<std::mutex> lock(buffer_pool_mutex_);
        if (buffer_pool_.size() < POOL_SIZE) {
            buffer_pool_.push_back(std::unique_ptr<char[]>(buffer));
        } else {
            delete[] buffer;
        }
    }
};

// Optimized network proxy with connection pooling
class OptimizedNetworkProxy : public NetworkProxy {
private:
    struct ConnectionPool {
        std::queue<int> available_connections;
        std::mutex pool_mutex;
        std::condition_variable pool_cv;
        size_t max_connections = 100;
        size_t current_connections = 0;
    };

    std::unordered_map<std::string, std::unique_ptr<ConnectionPool>> connection_pools_;

public:
    using NetworkProxy::NetworkProxy;

    bool exposePortOptimized(const std::string& containerId,
                            uint16_t containerPort,
                            uint16_t hostPort) override {
        // Use connection pooling for better performance
        auto pool = getConnectionPool(containerId);

        std::lock_guard<std::mutex> lock(pool->pool_mutex);
        if (pool->current_connections < pool->max_connections) {
            return exposePort(containerId, containerPort, hostPort);
        }

        return false; // Connection limit reached
    }

private:
    ConnectionPool* getConnectionPool(const std::string& containerId) {
        auto it = connection_pools_.find(containerId);
        if (it == connection_pools_.end()) {
            connection_pools_[containerId] = std::make_unique<ConnectionPool>();
            it = connection_pools_.find(containerId);
        }
        return it->second.get();
    }
};

} // namespace cross_platform
```

## Platform-Specific Considerations

### Windows-Specific Optimizations

1. **Hyper-V Integration**: Leverage Windows hypervisor for optimal performance
2. **WSL2 Backend**: Use Windows Subsystem for Linux 2 when available
3. **Named Pipes**: Use Windows named pipes for efficient IPC
4. **Filesystem Caching**: Implement Windows-specific file caching

### macOS-Specific Optimizations

1. **HyperKit**: Use lightweight macOS hypervisor
2. **virtio-fs**: Prefer virtio-fs over 9p for better performance
3. **vmnet**: Use Apple's vmnet framework for networking
4. **APFS Optimizations**: Leverage Apple File System features

### Cross-Platform Best Practices

1. **Resource Management**: Efficient memory and CPU allocation
2. **I/O Optimization**: Use platform-specific I/O primitives
3. **Network Efficiency**: Optimize for platform networking stack
4. **Security**: Maintain security across all platforms

This comprehensive cross-platform implementation enables Docker containers to run seamlessly on Windows and macOS while maintaining compatibility with Linux container ecosystems, providing developers with a consistent experience across all major platforms.
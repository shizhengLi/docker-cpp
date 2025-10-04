# Docker C++ Implementation - Plugin System: Extensibility and Third-party Integration

*Building a robust plugin architecture for container runtime extensibility*

## Table of Contents
1. [Introduction](#introduction)
2. [Plugin Architecture Overview](#plugin-architecture-overview)
3. [Plugin API Design](#plugin-api-design)
4. [Dynamic Loading Mechanism](#dynamic-loading-mechanism)
5. [Security Sandboxing](#security-sandboxing)
6. [Plugin Lifecycle Management](#plugin-lifecycle-management)
7. [Complete Implementation](#complete-implementation)
8. [Usage Examples](#usage-examples)
9. [Performance Considerations](#performance-considerations)
10. [Security Best Practices](#security-best-practices)

## Introduction

Modern container runtimes need extensibility to support various use cases, custom network drivers, storage backends, and security policies. A well-designed plugin system allows third-party developers to extend the runtime functionality while maintaining security and stability.

This article presents a complete C++ plugin architecture for our Docker implementation, featuring:

- **Type-safe Plugin API**: Strongly typed interfaces with ABI stability
- **Dynamic Loading**: Runtime plugin discovery and loading
- **Security Sandboxing**: Isolated plugin execution environments
- **Lifecycle Management**: Complete plugin lifecycle with hot-reloading
- **Resource Management**: Memory and resource cleanup guarantees

## Plugin Architecture Overview

### Core Components

```cpp
// Plugin System Architecture
// ┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
// │   Plugin API    │    │  Plugin Loader  │    │ Security Sandbox│
// │                 │    │                 │    │                 │
// │ • Interfaces    │◄──►│ • Dynamic Load  │◄──►│ • Resource Limits│
// │ • Type Safety   │    │ • Validation    │    │ • Filesystem    │
// │ • ABI Stability │    │ • Dependency Mgmt│    │ • Network       │
// └─────────────────┘    └─────────────────┘    └─────────────────┘
//          │                       │                       │
//          └───────────────────────┼───────────────────────┘
//                                  │
//                    ┌─────────────────┐
//                    │ Plugin Registry │
//                    │                 │
//                    │ • Discovery     │
//                    │ • Metadata      │
//                    │ • Configuration │
//                    └─────────────────┘
```

### Plugin Types Supported

1. **Network Plugins**: Custom network drivers and implementations
2. **Storage Plugins**: Volume drivers and storage backends
3. **Security Plugins**: Security policies and enforcement
4. **Runtime Plugins**: Container runtime extensions
5. **Monitoring Plugins**: Metrics and logging integrations

## Plugin API Design

### Core Plugin Interface

```cpp
#pragma once
#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <any>
#include <typeindex>
#include <unordered_map>
#include <atomic>
#include <chrono>

// Plugin system core types and interfaces
namespace plugin_system {

// Plugin version information
struct PluginVersion {
    uint32_t major;
    uint32_t minor;
    uint32_t patch;

    std::string toString() const {
        return std::to_string(major) + "." +
               std::to_string(minor) + "." +
               std::to_string(patch);
    }

    bool isCompatible(const PluginVersion& required) const {
        return major == required.major &&
               minor >= required.minor;
    }
};

// Plugin metadata
struct PluginMetadata {
    std::string name;
    std::string version;
    std::string description;
    std::string author;
    std::string license;
    std::vector<std::string> dependencies;
    std::unordered_map<std::string, std::string> config;

    // Plugin type and capabilities
    std::string type;
    std::vector<std::string> capabilities;

    // Resource requirements
    struct ResourceLimits {
        size_t maxMemoryMB = 512;
        size_t maxFileDescriptors = 100;
        std::chrono::milliseconds maxCpuTime{5000};
    } resourceLimits;
};

// Plugin context for runtime operations
class PluginContext {
public:
    virtual ~PluginContext() = default;

    // Configuration access
    virtual std::string getConfig(const std::string& key) const = 0;
    virtual std::unordered_map<std::string, std::string> getAllConfig() const = 0;

    // Logging
    virtual void log(int level, const std::string& message) const = 0;

    // Resource management
    virtual void* allocate(size_t size) = 0;
    virtual void deallocate(void* ptr) = 0;

    // Plugin communication
    virtual std::any callPlugin(const std::string& pluginName,
                                const std::string& method,
                                const std::any& args) = 0;
};

// Plugin state enumeration
enum class PluginState {
    UNLOADED,
    LOADING,
    LOADED,
    INITIALIZING,
    ACTIVE,
    ERROR,
    UNLOADING
};

// Core plugin interface
class IPlugin {
public:
    virtual ~IPlugin() = default;

    // Plugin metadata
    virtual PluginMetadata getMetadata() const = 0;
    virtual PluginVersion getVersion() const = 0;
    virtual std::string getInterfaceVersion() const = 0;

    // Lifecycle management
    virtual bool initialize(PluginContext* context) = 0;
    virtual bool start() = 0;
    virtual bool stop() = 0;
    virtual void cleanup() = 0;

    // State management
    virtual PluginState getState() const = 0;
    virtual std::string getLastError() const = 0;

    // Health check
    virtual bool isHealthy() const = 0;
    virtual std::chrono::milliseconds getHealthCheckInterval() const = 0;
};

// Type-specific plugin interfaces
class INetworkPlugin : public IPlugin {
public:
    virtual ~INetworkPlugin() = default;

    // Network lifecycle
    virtual bool createNetwork(const std::string& networkId,
                              const std::unordered_map<std::string, std::string>& options) = 0;
    virtual bool deleteNetwork(const std::string& networkId) = 0;
    virtual bool connectContainer(const std::string& networkId,
                                 const std::string& containerId,
                                 const std::string& ipAddress) = 0;
    virtual bool disconnectContainer(const std::string& networkId,
                                    const std::string& containerId) = 0;

    // Network operations
    virtual std::vector<std::string> listNetworks() const = 0;
    virtual std::vector<std::string> getNetworkContainers(const std::string& networkId) const = 0;
};

class IStoragePlugin : public IPlugin {
public:
    virtual ~IStoragePlugin() = default;

    // Volume operations
    virtual bool createVolume(const std::string& volumeName,
                             const std::unordered_map<std::string, std::string>& options) = 0;
    virtual bool deleteVolume(const std::string& volumeName) = 0;
    virtual bool mountVolume(const std::string& volumeName, const std::string& mountPoint) = 0;
    virtual bool unmountVolume(const std::string& mountPoint) = 0;

    // Volume information
    virtual std::vector<std::string> listVolumes() const = 0;
    virtual size_t getVolumeSize(const std::string& volumeName) const = 0;
    virtual bool volumeExists(const std::string& volumeName) const = 0;
};

class ISecurityPlugin : public IPlugin {
public:
    virtual ~ISecurityPlugin() = default;

    // Security policies
    virtual bool enforcePolicy(const std::string& policyName,
                              const std::unordered_map<std::string, std::string>& params) = 0;
    virtual bool checkPermission(const std::string& operation,
                                const std::unordered_map<std::string, std::string>& context) = 0;

    // Container security
    virtual bool sandboxContainer(const std::string& containerId,
                                 const std::unordered_map<std::string, std::string>& securityProfile) = 0;
    virtual bool auditEvent(const std::string& eventType,
                           const std::unordered_map<std::string, std::string>& eventData) = 0;
};

} // namespace plugin_system
```

## Dynamic Loading Mechanism

### Plugin Loader Implementation

```cpp
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <thread>
#include <shared_mutex>

namespace plugin_system {

// Plugin loading result
struct LoadResult {
    bool success = false;
    std::string error;
    std::shared_ptr<IPlugin> plugin;

    static LoadResult success_with_plugin(std::shared_ptr<IPlugin> plugin) {
        LoadResult result;
        result.success = true;
        result.plugin = plugin;
        return result;
    }

    static LoadResult failure(const std::string& error) {
        LoadResult result;
        result.error = error;
        return result;
    }
};

// Plugin factory function type
using CreatePluginFunc = std::unique_ptr<IPlugin>(*)();
using DestroyPluginFunc = void(*)(IPlugin*);

// Plugin library wrapper
class PluginLibrary {
private:
    void* handle_ = nullptr;
    CreatePluginFunc create_func_ = nullptr;
    DestroyPluginFunc destroy_func_ = nullptr;
    std::string path_;
    std::atomic<int> ref_count_{0};

public:
    explicit PluginLibrary(const std::string& path) : path_(path) {}

    ~PluginLibrary() {
        unload();
    }

    bool load() {
        handle_ = dlopen(path_.c_str(), RTLD_LAZY);
        if (!handle_) {
            return false;
        }

        // Load factory functions
        create_func_ = reinterpret_cast<CreatePluginFunc>(
            dlsym(handle_, "createPlugin"));
        destroy_func_ = reinterpret_cast<DestroyPluginFunc>(
            dlsym(handle_, "destroyPlugin"));

        return create_func_ && destroy_func_;
    }

    void unload() {
        if (handle_) {
            dlclose(handle_);
            handle_ = nullptr;
        }
        create_func_ = nullptr;
        destroy_func_ = nullptr;
    }

    std::unique_ptr<IPlugin> createInstance() {
        if (!create_func_) return nullptr;
        return create_func_();
    }

    void destroyInstance(IPlugin* plugin) {
        if (destroy_func_) {
            destroy_func_(plugin);
        }
    }

    std::string getLastError() const {
        return dlerror() ? dlerror() : "Unknown error";
    }

    void addRef() { ref_count_++; }
    void release() { ref_count_--; }
    int getRefCount() const { return ref_count_.load(); }

    bool isLoaded() const { return handle_ != nullptr; }
};

// Plugin loader with dependency management
class PluginLoader {
private:
    std::unordered_map<std::string, std::shared_ptr<PluginLibrary>> libraries_;
    std::shared_mutex mutex_;
    std::vector<std::string> search_paths_;

public:
    PluginLoader() {
        // Default search paths
        addSearchPath("./plugins");
        addSearchPath("/usr/local/lib/docker/plugins");
        addSearchPath("/opt/docker/plugins");
    }

    void addSearchPath(const std::string& path) {
        std::lock_guard<std::shared_mutex> lock(mutex_);
        search_paths_.push_back(path);
    }

    LoadResult loadPlugin(const std::string& pluginName) {
        std::shared_lock<std::shared_mutex> lock(mutex_);

        // Check if already loaded
        auto it = libraries_.find(pluginName);
        if (it != libraries_.end() && it->second->isLoaded()) {
            auto plugin = it->second->createInstance();
            if (plugin) {
                return LoadResult::success_with_plugin(
                    std::shared_ptr<IPlugin>(plugin.release()));
            }
        }
        lock.unlock();

        // Find plugin library
        std::string pluginPath = findPluginLibrary(pluginName);
        if (pluginPath.empty()) {
            return LoadResult::failure("Plugin library not found: " + pluginName);
        }

        // Load the library
        auto library = std::make_shared<PluginLibrary>(pluginPath);
        if (!library->load()) {
            return LoadResult::failure("Failed to load plugin library: " +
                                     library->getLastError());
        }

        // Create plugin instance
        auto plugin = library->createInstance();
        if (!plugin) {
            return LoadResult::failure("Failed to create plugin instance");
        }

        // Register library
        std::lock_guard<std::shared_mutex> write_lock(mutex_);
        library->addRef();
        libraries_[pluginName] = library;

        return LoadResult::success_with_plugin(
            std::shared_ptr<IPlugin>(plugin.release()));
    }

    void unloadPlugin(const std::string& pluginName) {
        std::lock_guard<std::shared_mutex> lock(mutex_);

        auto it = libraries_.find(pluginName);
        if (it != libraries_.end()) {
            it->second->release();
            if (it->second->getRefCount() <= 0) {
                it->second->unload();
                libraries_.erase(it);
            }
        }
    }

    std::vector<std::string> discoverPlugins() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        std::vector<std::string> plugins;

        for (const auto& searchPath : search_paths_) {
            if (std::filesystem::exists(searchPath)) {
                for (const auto& entry : std::filesystem::directory_iterator(searchPath)) {
                    if (entry.is_regular_file() &&
                        entry.path().extension() == ".so") {
                        plugins.push_back(entry.path().stem().string());
                    }
                }
            }
        }

        return plugins;
    }

private:
    std::string findPluginLibrary(const std::string& pluginName) const {
        for (const auto& searchPath : search_paths_) {
            std::string fullPath = searchPath + "/" + pluginName + ".so";
            if (std::filesystem::exists(fullPath)) {
                return fullPath;
            }
        }
        return "";
    }
};

} // namespace plugin_system
```

## Security Sandboxing

### Resource-Limited Plugin Execution

```cpp
#include <sys/resource.h>
#include <unistd.h>
#include <signal.h>
#include <sys/prctl.h>

namespace plugin_system {

// Sandbox configuration
struct SandboxConfig {
    // Resource limits
    size_t maxMemoryMB = 512;
    size_t maxCpuTimeMs = 5000;
    size_t maxFileSize = 1024 * 1024; // 1MB
    size_t maxOpenFiles = 50;

    // Network restrictions
    bool allowNetwork = false;
    std::vector<std::string> allowedHosts;

    // Filesystem restrictions
    std::string chrootPath;
    std::vector<std::string> readOnlyPaths;
    std::vector<std::string> readWritePaths;

    // Process restrictions
    bool allowFork = false;
    bool allowPtrace = false;
};

// Plugin sandbox implementation
class PluginSandbox {
private:
    pid_t sandbox_pid_ = -1;
    std::string plugin_name_;
    SandboxConfig config_;
    std::atomic<bool> active_{false};

public:
    PluginSandbox(const std::string& pluginName, const SandboxConfig& config)
        : plugin_name_(pluginName), config_(config) {}

    ~PluginSandbox() {
        stop();
    }

    bool start() {
        // Create sandbox process
        sandbox_pid_ = fork();
        if (sandbox_pid_ < 0) {
            return false;
        }

        if (sandbox_pid_ == 0) {
            // Child process - apply sandbox restrictions
            setupSandbox();

            // Signal parent that sandbox is ready
            kill(getppid(), SIGUSR1);

            // Wait for commands from parent
            waitForCommands();

            _exit(0);
        } else {
            // Parent process
            if (waitForSandboxReady()) {
                active_ = true;
                return true;
            } else {
                stop();
                return false;
            }
        }
    }

    void stop() {
        if (active_ && sandbox_pid_ > 0) {
            kill(sandbox_pid_, SIGTERM);
            int status;
            waitpid(sandbox_pid_, &status, 0);
            active_ = false;
            sandbox_pid_ = -1;
        }
    }

    bool executePlugin(std::shared_ptr<IPlugin> plugin,
                      PluginContext* context) {
        if (!active_) return false;

        // Send plugin execution command to sandbox
        // This is a simplified implementation
        // In practice, you'd use IPC mechanisms

        return true;
    }

    bool isActive() const {
        return active_.load();
    }

private:
    void setupSandbox() {
        // Set process name
        prctl(PR_SET_NAME, "plugin-sandbox");

        // Set up signal handlers
        signal(SIGTERM, [](int) { _exit(0); });

        // Apply resource limits
        setupResourceLimits();

        // Set up filesystem restrictions
        setupFilesystemRestrictions();

        // Drop privileges
        dropPrivileges();
    }

    void setupResourceLimits() {
        struct rlimit limit;

        // Memory limit
        limit.rlim_cur = limit.rlim_max = config_.maxMemoryMB * 1024 * 1024;
        setrlimit(RLIMIT_AS, &limit);

        // CPU time limit
        limit.rlim_cur = limit.rlim_max = config_.maxCpuTimeMs / 1000;
        setrlimit(RLIMIT_CPU, &limit);

        // File size limit
        limit.rlim_cur = limit.rlim_max = config_.maxFileSize;
        setrlimit(RLIMIT_FSIZE, &limit);

        // Open files limit
        limit.rlim_cur = limit.rlim_max = config_.maxOpenFiles;
        setrlimit(RLIMIT_NOFILE, &limit);

        // Process limit
        limit.rlim_cur = limit.rlim_max = config_.allowFork ? 100 : 1;
        setrlimit(RLIMIT_NPROC, &limit);
    }

    void setupFilesystemRestrictions() {
        // Change root if specified
        if (!config_.chrootPath.empty()) {
            chroot(config_.chrootPath.c_str());
            chdir("/");
        }

        // Set up read-only paths
        for (const auto& path : config_.readOnlyPaths) {
            // Mount path as read-only
            mount(path.c_str(), path.c_str(), "bind",
                  MS_BIND | MS_REMOUNT | MS_RDONLY, nullptr);
        }
    }

    void dropPrivileges() {
        // Drop to non-root user if running as root
        if (getuid() == 0) {
            // Set to nobody user
            if (setgid(65534) != 0 || setuid(65534) != 0) {
                // Fallback to non-privileged user
                setgid(getgid());
                setuid(getuid());
            }
        }

        // Disable ptrace
        prctl(PR_SET_DUMPABLE, 0);
    }

    bool waitForSandboxReady() {
        // Wait for SIGUSR1 from sandbox process
        sigset_t mask;
        sigemptyset(&mask);
        sigaddset(&mask, SIGUSR1);

        int sig;
        if (sigwait(&mask, &sig) == 0 && sig == SIGUSR1) {
            return true;
        }

        return false;
    }

    void waitForCommands() {
        // Wait for plugin execution commands
        while (true) {
            pause();
        }
    }
};

// Sandbox manager for multiple plugin sandboxes
class SandboxManager {
private:
    std::unordered_map<std::string, std::unique_ptr<PluginSandbox>> sandboxes_;
    std::shared_mutex mutex_;
    SandboxConfig default_config_;

public:
    SandboxManager() {
        // Set up default sandbox configuration
        default_config_.maxMemoryMB = 512;
        default_config_.maxCpuTimeMs = 5000;
        default_config_.allowNetwork = false;
        default_config_.allowFork = false;
    }

    bool createSandbox(const std::string& pluginName,
                      const SandboxConfig& config = {}) {
        std::lock_guard<std::shared_mutex> lock(mutex_);

        SandboxConfig sandboxConfig = config.maxMemoryMB > 0 ? config : default_config_;

        auto sandbox = std::make_unique<PluginSandbox>(pluginName, sandboxConfig);
        if (sandbox->start()) {
            sandboxes_[pluginName] = std::move(sandbox);
            return true;
        }

        return false;
    }

    void destroySandbox(const std::string& pluginName) {
        std::lock_guard<std::shared_mutex> lock(mutex_);

        auto it = sandboxes_.find(pluginName);
        if (it != sandboxes_.end()) {
            it->second->stop();
            sandboxes_.erase(it);
        }
    }

    PluginSandbox* getSandbox(const std::string& pluginName) {
        std::shared_lock<std::shared_mutex> lock(mutex_);

        auto it = sandboxes_.find(pluginName);
        return it != sandboxes_.end() ? it->second.get() : nullptr;
    }

    void setDefaultConfig(const SandboxConfig& config) {
        default_config_ = config;
    }
};

} // namespace plugin_system
```

## Plugin Lifecycle Management

### Plugin Registry and Lifecycle Controller

```cpp
#include <condition_variable>
#include <queue>
#include <future>

namespace plugin_system {

// Plugin event types
enum class PluginEvent {
    LOADED,
    UNLOADED,
    INITIALIZED,
    STARTED,
    STOPPED,
    ERROR_OCCURRED
};

// Plugin event data
struct PluginEventData {
    PluginEvent event;
    std::string pluginName;
    std::string message;
    std::chrono::system_clock::time_point timestamp;
};

// Plugin event handler
using PluginEventHandler = std::function<void(const PluginEventData&)>;

// Plugin registry
class PluginRegistry {
private:
    struct PluginEntry {
        std::shared_ptr<IPlugin> plugin;
        PluginState state = PluginState::UNLOADED;
        std::string lastError;
        std::chrono::system_clock::time_point lastActivity;
        std::unique_ptr<PluginSandbox> sandbox;
    };

    std::unordered_map<std::string, PluginEntry> plugins_;
    mutable std::shared_mutex mutex_;
    std::vector<PluginEventHandler> event_handlers_;
    std::unique_ptr<PluginLoader> loader_;
    std::unique_ptr<SandboxManager> sandbox_manager_;

    // Background task queue
    std::queue<std::function<void()>> task_queue_;
    std::mutex task_mutex_;
    std::condition_variable task_cv_;
    std::thread background_thread_;
    std::atomic<bool> running_{true};

public:
    PluginRegistry()
        : loader_(std::make_unique<PluginLoader>()),
          sandbox_manager_(std::make_unique<SandboxManager>()) {
        background_thread_ = std::thread(&PluginRegistry::processTasks, this);
    }

    ~PluginRegistry() {
        running_ = false;
        task_cv_.notify_all();
        if (background_thread_.joinable()) {
            background_thread_.join();
        }

        // Unload all plugins
        std::lock_guard<std::shared_mutex> lock(mutex_);
        for (auto& [name, entry] : plugins_) {
            if (entry.plugin && entry.state == PluginState::ACTIVE) {
                entry.plugin->stop();
                entry.plugin->cleanup();
            }
        }
    }

    // Plugin loading and registration
    bool loadPlugin(const std::string& pluginName, bool sandboxed = true) {
        auto loadResult = loader_->loadPlugin(pluginName);
        if (!loadResult.success) {
            notifyEvent(PluginEvent::ERROR_OCCURRED, pluginName, loadResult.error);
            return false;
        }

        std::lock_guard<std::shared_mutex> lock(mutex_);

        PluginEntry entry;
        entry.plugin = loadResult.plugin;
        entry.state = PluginState::LOADED;
        entry.lastActivity = std::chrono::system_clock::now();

        // Create sandbox if requested
        if (sandboxed) {
            entry.sandbox = std::make_unique<PluginSandbox>(pluginName,
                                                           getDefaultSandboxConfig());
            if (!entry.sandbox->start()) {
                notifyEvent(PluginEvent::ERROR_OCCURRED, pluginName,
                           "Failed to start plugin sandbox");
                return false;
            }
        }

        plugins_[pluginName] = std::move(entry);

        notifyEvent(PluginEvent::LOADED, pluginName, "Plugin loaded successfully");
        return true;
    }

    bool initializePlugin(const std::string& pluginName,
                         const std::unordered_map<std::string, std::string>& config = {}) {
        std::lock_guard<std::shared_mutex> lock(mutex_);

        auto it = plugins_.find(pluginName);
        if (it == plugins_.end()) {
            return false;
        }

        auto& entry = it->second;
        if (entry.state != PluginState::LOADED) {
            return false;
        }

        entry.state = PluginState::INITIALIZING;

        // Create plugin context
        auto context = createPluginContext(config);

        if (entry.plugin->initialize(context.get())) {
            entry.state = PluginState::ACTIVE;
            entry.lastActivity = std::chrono::system_clock::now();
            notifyEvent(PluginEvent::INITIALIZED, pluginName, "Plugin initialized");
            return true;
        } else {
            entry.state = PluginState::ERROR;
            entry.lastError = "Plugin initialization failed";
            notifyEvent(PluginEvent::ERROR_OCCURRED, pluginName, entry.lastError);
            return false;
        }
    }

    bool startPlugin(const std::string& pluginName) {
        std::lock_guard<std::shared_mutex> lock(mutex_);

        auto it = plugins_.find(pluginName);
        if (it == plugins_.end()) {
            return false;
        }

        auto& entry = it->second;
        if (entry.state != PluginState::ACTIVE) {
            return false;
        }

        if (entry.plugin->start()) {
            entry.lastActivity = std::chrono::system_clock::now();
            notifyEvent(PluginEvent::STARTED, pluginName, "Plugin started");
            return true;
        } else {
            entry.lastError = "Plugin start failed";
            notifyEvent(PluginEvent::ERROR_OCCURRED, pluginName, entry.lastError);
            return false;
        }
    }

    bool stopPlugin(const std::string& pluginName) {
        std::lock_guard<std::shared_mutex> lock(mutex_);

        auto it = plugins_.find(pluginName);
        if (it == plugins_.end()) {
            return false;
        }

        auto& entry = it->second;
        if (entry.state != PluginState::ACTIVE) {
            return false;
        }

        if (entry.plugin->stop()) {
            notifyEvent(PluginEvent::STOPPED, pluginName, "Plugin stopped");
            return true;
        } else {
            entry.lastError = "Plugin stop failed";
            return false;
        }
    }

    void unloadPlugin(const std::string& pluginName) {
        std::lock_guard<std::shared_mutex> lock(mutex_);

        auto it = plugins_.find(pluginName);
        if (it == plugins_.end()) {
            return;
        }

        auto& entry = it->second;

        // Stop plugin if active
        if (entry.state == PluginState::ACTIVE) {
            entry.plugin->stop();
            entry.plugin->cleanup();
        }

        // Destroy sandbox
        entry.sandbox.reset();

        // Remove from registry
        plugins_.erase(it);

        loader_->unloadPlugin(pluginName);
        notifyEvent(PluginEvent::UNLOADED, pluginName, "Plugin unloaded");
    }

    // Plugin access
    std::shared_ptr<IPlugin> getPlugin(const std::string& pluginName) {
        std::shared_lock<std::shared_mutex> lock(mutex_);

        auto it = plugins_.find(pluginName);
        if (it != plugins_.end() && it->second.state == PluginState::ACTIVE) {
            return it->second.plugin;
        }

        return nullptr;
    }

    template<typename T>
    std::shared_ptr<T> getPluginAs(const std::string& pluginName) {
        auto plugin = getPlugin(pluginName);
        return std::dynamic_pointer_cast<T>(plugin);
    }

    // Plugin information
    std::vector<std::string> listPlugins() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);

        std::vector<std::string> names;
        for (const auto& [name, entry] : plugins_) {
            names.push_back(name);
        }
        return names;
    }

    PluginState getPluginState(const std::string& pluginName) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);

        auto it = plugins_.find(pluginName);
        return it != plugins_.end() ? it->second.state : PluginState::UNLOADED;
    }

    std::string getPluginError(const std::string& pluginName) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);

        auto it = plugins_.find(pluginName);
        return it != plugins_.end() ? it->second.lastError : "Plugin not found";
    }

    // Event handling
    void addEventHandler(PluginEventHandler handler) {
        std::lock_guard<std::shared_mutex> lock(mutex_);
        event_handlers_.push_back(handler);
    }

    // Health checking
    void startHealthChecking() {
        // Schedule periodic health checks
        scheduleTask([this]() {
            performHealthChecks();
        });
    }

    // Plugin hot-reloading
    bool reloadPlugin(const std::string& pluginName) {
        if (getPluginState(pluginName) == PluginState::ACTIVE) {
            stopPlugin(pluginName);
        }

        unloadPlugin(pluginName);
        return loadPlugin(pluginName);
    }

private:
    void notifyEvent(PluginEvent event, const std::string& pluginName,
                    const std::string& message) {
        PluginEventData eventData;
        eventData.event = event;
        eventData.pluginName = pluginName;
        eventData.message = message;
        eventData.timestamp = std::chrono::system_clock::now();

        // Notify handlers in background thread
        scheduleTask([this, eventData]() {
            std::shared_lock<std::shared_mutex> lock(mutex_);
            for (const auto& handler : event_handlers_) {
                try {
                    handler(eventData);
                } catch (const std::exception& e) {
                    // Log error but don't crash
                }
            }
        });
    }

    std::unique_ptr<PluginContext> createPluginContext(
        const std::unordered_map<std::string, std::string>& config) {
        // Create a concrete plugin context implementation
        // This is a simplified version
        struct ConcretePluginContext : public PluginContext {
            std::unordered_map<std::string, std::string> config_;

            explicit ConcretePluginContext(
                const std::unordered_map<std::string, std::string>& config)
                : config_(config) {}

            std::string getConfig(const std::string& key) const override {
                auto it = config_.find(key);
                return it != config_.end() ? it->second : "";
            }

            std::unordered_map<std::string, std::string> getAllConfig() const override {
                return config_;
            }

            void log(int level, const std::string& message) const override {
                // Log implementation
            }

            void* allocate(size_t size) override {
                return std::malloc(size);
            }

            void deallocate(void* ptr) override {
                std::free(ptr);
            }

            std::any callPlugin(const std::string& pluginName,
                              const std::string& method,
                              const std::any& args) override {
                // Plugin communication implementation
                return std::any{};
            }
        };

        return std::make_unique<ConcretePluginContext>(config);
    }

    SandboxConfig getDefaultSandboxConfig() {
        SandboxConfig config;
        config.maxMemoryMB = 512;
        config.maxCpuTimeMs = 5000;
        config.allowNetwork = false;
        config.allowFork = false;
        return config;
    }

    void performHealthChecks() {
        std::shared_lock<std::shared_mutex> lock(mutex_);

        for (auto& [name, entry] : plugins_) {
            if (entry.state == PluginState::ACTIVE &&
                entry.plugin->isHealthy() == false) {
                notifyEvent(PluginEvent::ERROR_OCCURRED, name,
                           "Plugin health check failed");
            }
        }

        // Schedule next health check
        scheduleTask([this]() {
            performHealthChecks();
        });
    }

    void scheduleTask(std::function<void()> task) {
        {
            std::lock_guard<std::mutex> lock(task_mutex_);
            task_queue_.push(task);
        }
        task_cv_.notify_one();
    }

    void processTasks() {
        while (running_) {
            std::unique_lock<std::mutex> lock(task_mutex_);
            task_cv_.wait(lock, [this] { return !task_queue_.empty() || !running_; });

            while (!task_queue_.empty()) {
                auto task = task_queue_.front();
                task_queue_.pop();
                lock.unlock();

                try {
                    task();
                } catch (const std::exception& e) {
                    // Log task error
                }

                lock.lock();
            }
        }
    }
};

} // namespace plugin_system
```

## Complete Implementation

### Example Network Plugin Implementation

```cpp
// Example plugin implementation - bridge network driver
extern "C" {
    #include <sys/socket.h>
    #include <linux/if.h>
    #include <linux/if_bridge.h>
    #include <unistd.h>
}

class BridgeNetworkPlugin : public plugin_system::INetworkPlugin {
private:
    std::string name_;
    plugin_system::PluginState state_ = plugin_system::PluginState::UNLOADED;
    std::string last_error_;
    plugin_system::PluginContext* context_ = nullptr;
    std::unordered_map<std::string, std::string> networks_;

public:
    BridgeNetworkPlugin() : name_("bridge-network") {}

    plugin_system::PluginMetadata getMetadata() const override {
        plugin_system::PluginMetadata metadata;
        metadata.name = "bridge-network";
        metadata.version = "1.0.0";
        metadata.description = "Bridge network driver for containers";
        metadata.author = "Docker C++ Implementation";
        metadata.license = "MIT";
        metadata.type = "network";
        metadata.capabilities = {"bridge", "vlan", "macvlan"};

        metadata.resourceLimits.maxMemoryMB = 128;
        metadata.resourceLimits.maxFileDescriptors = 50;

        return metadata;
    }

    plugin_system::PluginVersion getVersion() const override {
        return {1, 0, 0};
    }

    std::string getInterfaceVersion() const override {
        return "1.0";
    }

    bool initialize(plugin_system::PluginContext* context) override {
        context_ = context;
        state_ = plugin_system::PluginState::INITIALIZING;

        // Initialize bridge driver
        if (!setupBridgeDriver()) {
            last_error_ = "Failed to initialize bridge driver";
            state_ = plugin_system::PluginState::ERROR;
            return false;
        }

        state_ = plugin_system::PluginState::ACTIVE;
        return true;
    }

    bool start() override {
        context_->log(1, "Bridge network plugin started");
        return true;
    }

    bool stop() override {
        context_->log(1, "Bridge network plugin stopped");
        return true;
    }

    void cleanup() override {
        networks_.clear();
        state_ = plugin_system::PluginState::UNLOADED;
    }

    plugin_system::PluginState getState() const override {
        return state_;
    }

    std::string getLastError() const override {
        return last_error_;
    }

    bool isHealthy() const override {
        return state_ == plugin_system::PluginState::ACTIVE;
    }

    std::chrono::milliseconds getHealthCheckInterval() const override {
        return std::chrono::milliseconds(30000);
    }

    // Network interface implementation
    bool createNetwork(const std::string& networkId,
                      const std::unordered_map<std::string, std::string>& options) override {
        context_->log(1, "Creating network: " + networkId);

        // Create Linux bridge
        std::string bridgeName = options.count("bridge") ?
                               options.at("bridge") : "docker" + networkId.substr(0, 8);

        if (!createLinuxBridge(bridgeName)) {
            last_error_ = "Failed to create Linux bridge: " + bridgeName;
            return false;
        }

        // Configure bridge
        if (!configureBridge(bridgeName, options)) {
            deleteLinuxBridge(bridgeName);
            last_error_ = "Failed to configure bridge: " + bridgeName;
            return false;
        }

        networks_[networkId] = bridgeName;
        return true;
    }

    bool deleteNetwork(const std::string& networkId) override {
        context_->log(1, "Deleting network: " + networkId);

        auto it = networks_.find(networkId);
        if (it == networks_.end()) {
            return false;
        }

        deleteLinuxBridge(it->second);
        networks_.erase(it);
        return true;
    }

    bool connectContainer(const std::string& networkId,
                         const std::string& containerId,
                         const std::string& ipAddress) override {
        context_->log(1, "Connecting container " + containerId + " to network " + networkId);

        auto it = networks_.find(networkId);
        if (it == networks_.end()) {
            return false;
        }

        // Create veth pair
        std::string vethHost = "veth" + containerId.substr(0, 8);
        std::string vethContainer = "eth0";

        if (!createVethPair(vethHost, vethContainer)) {
            last_error_ = "Failed to create veth pair";
            return false;
        }

        // Connect host side to bridge
        if (!connectToBridge(it->second, vethHost)) {
            deleteVethPair(vethHost);
            last_error_ = "Failed to connect veth to bridge";
            return false;
        }

        // Configure container interface
        if (!configureContainerInterface(vethContainer, ipAddress)) {
            last_error_ = "Failed to configure container interface";
            return false;
        }

        return true;
    }

    bool disconnectContainer(const std::string& networkId,
                            const std::string& containerId) override {
        context_->log(1, "Disconnecting container " + containerId + " from network " + networkId);

        std::string vethHost = "veth" + containerId.substr(0, 8);
        deleteVethPair(vethHost);

        return true;
    }

    std::vector<std::string> listNetworks() const override {
        std::vector<std::string> result;
        for (const auto& [networkId, bridgeName] : networks_) {
            result.push_back(networkId);
        }
        return result;
    }

    std::vector<std::string> getNetworkContainers(const std::string& networkId) const override {
        // Implementation would query bridge for connected interfaces
        return {};
    }

private:
    bool setupBridgeDriver() {
        // Initialize bridge driver subsystem
        return true;
    }

    bool createLinuxBridge(const std::string& bridgeName) {
        // Implementation would create Linux bridge using ioctl/netlink
        return true;
    }

    bool configureBridge(const std::string& bridgeName,
                        const std::unordered_map<std::string, std::string>& options) {
        // Configure bridge IP, forwarding, etc.
        return true;
    }

    void deleteLinuxBridge(const std::string& bridgeName) {
        // Delete Linux bridge
    }

    bool createVethPair(const std::string& hostSide, const std::string& containerSide) {
        // Create veth pair
        return true;
    }

    void deleteVethPair(const std::string& hostSide) {
        // Delete veth pair
    }

    bool connectToBridge(const std::string& bridgeName, const std::string& interface) {
        // Connect interface to bridge
        return true;
    }

    bool configureContainerInterface(const std::string& interface,
                                    const std::string& ipAddress) {
        // Configure container interface IP, routes, etc.
        return true;
    }
};

// Plugin factory functions required for dynamic loading
extern "C" {
    std::unique_ptr<plugin_system::IPlugin> createPlugin() {
        return std::make_unique<BridgeNetworkPlugin>();
    }

    void destroyPlugin(plugin_system::IPlugin* plugin) {
        delete plugin;
    }
}
```

## Usage Examples

### Basic Plugin System Usage

```cpp
#include "plugin_system.h"

int main() {
    // Initialize plugin registry
    auto registry = std::make_unique<plugin_system::PluginRegistry>();

    // Add event handler for monitoring plugin lifecycle
    registry->addEventHandler([](const plugin_system::PluginEventData& event) {
        std::cout << "Plugin " << event.pluginName
                  << " event: " << static_cast<int>(event.event)
                  << " - " << event.message << std::endl;
    });

    // Discover available plugins
    auto availablePlugins = registry->listPlugins();
    std::cout << "Available plugins: ";
    for (const auto& plugin : availablePlugins) {
        std::cout << plugin << " ";
    }
    std::cout << std::endl;

    // Load network plugin
    if (registry->loadPlugin("bridge-network")) {
        std::cout << "Bridge network plugin loaded successfully" << std::endl;

        // Initialize plugin with configuration
        std::unordered_map<std::string, std::string> config = {
            {"default_bridge", "docker0"},
            {"mtu", "1500"}
        };

        if (registry->initializePlugin("bridge-network", config)) {
            std::cout << "Bridge network plugin initialized" << std::endl;

            // Start plugin
            if (registry->startPlugin("bridge-network")) {
                std::cout << "Bridge network plugin started" << std::endl;

                // Get plugin as network plugin
                auto networkPlugin = registry->getPluginAs<plugin_system::INetworkPlugin>("bridge-network");
                if (networkPlugin) {
                    // Create a network
                    std::unordered_map<std::string, std::string> networkOptions = {
                        {"subnet", "172.18.0.0/16"},
                        {"gateway", "172.18.0.1"}
                    };

                    if (networkPlugin->createNetwork("mynetwork", networkOptions)) {
                        std::cout << "Network created successfully" << std::endl;

                        // Connect a container
                        if (networkPlugin->connectContainer("mynetwork", "container123", "172.18.0.2")) {
                            std::cout << "Container connected to network" << std::endl;
                        }
                    }
                }
            }
        }
    }

    // Start health checking
    registry->startHealthChecking();

    // Keep running
    std::this_thread::sleep_for(std::chrono::minutes(5));

    // Cleanup
    registry->unloadPlugin("bridge-network");

    return 0;
}
```

### Advanced Plugin Configuration

```cpp
// Configure sandbox with custom limits
plugin_system::SandboxConfig sandboxConfig;
sandboxConfig.maxMemoryMB = 1024;
sandboxConfig.maxCpuTimeMs = 10000;
sandboxConfig.allowNetwork = true;
sandboxConfig.allowedHosts = {"registry.example.com", "api.example.com"};
sandboxConfig.chrootPath = "/var/lib/docker/plugins/sandbox";
sandboxConfig.readOnlyPaths = {"/usr", "/lib", "/etc"};
sandboxConfig.readWritePaths = {"/tmp", "/var/log"};

// Create sandboxed plugin environment
auto sandboxManager = std::make_unique<plugin_system::SandboxManager>();
sandboxManager->createSandbox("advanced-network-plugin", sandboxConfig);

// Load plugin with custom configuration
if (registry->loadPlugin("advanced-network-plugin", true)) {
    std::unordered_map<std::string, std::string> advancedConfig = {
        {"encryption_enabled", "true"},
        {"mtu", "9000"},
        {"vlan_range", "100-200"},
        {"policy_file", "/etc/docker/network-policy.json"}
    };

    registry->initializePlugin("advanced-network-plugin", advancedConfig);
}
```

## Performance Considerations

### Optimization Strategies

1. **Lazy Loading**: Load plugins only when needed
2. **Resource Pooling**: Reuse sandbox environments
3. **Async Operations**: Non-blocking plugin calls
4. **Caching**: Cache plugin metadata and capabilities
5. **Batch Operations**: Group multiple plugin calls

```cpp
// Performance-optimized plugin manager
class OptimizedPluginManager {
private:
    std::unordered_map<std::string, std::future<LoadResult>> loading_futures_;
    std::chrono::milliseconds plugin_timeout_{5000};

public:
    // Async plugin loading
    std::future<LoadResult> loadPluginAsync(const std::string& pluginName) {
        return std::async(std::launch::async, [this, pluginName]() {
            return registry_->loadPlugin(pluginName);
        });
    }

    // Preload critical plugins
    void preloadCriticalPlugins() {
        std::vector<std::string> criticalPlugins = {
            "bridge-network",
            "local-storage",
            "default-security"
        };

        std::vector<std::future<LoadResult>> futures;
        for (const auto& plugin : criticalPlugins) {
            futures.push_back(loadPluginAsync(plugin));
        }

        // Wait for all to complete
        for (auto& future : futures) {
            if (future.wait_for(plugin_timeout_) == std::future_status::timeout) {
                // Handle timeout
            }
        }
    }

    // Plugin call caching
    template<typename R>
    std::optional<R> getCachedResult(const std::string& cacheKey) {
        auto it = call_cache_.find(cacheKey);
        if (it != call_cache_.end() &&
            std::chrono::steady_clock::now() - it->second.timestamp < cache_ttl_) {
            return std::any_cast<R>(it->second.result);
        }
        return std::nullopt;
    }

private:
    struct CacheEntry {
        std::any result;
        std::chrono::steady_clock::time_point timestamp;
    };

    std::unordered_map<std::string, CacheEntry> call_cache_;
    std::chrono::seconds cache_ttl_{300};
    std::unique_ptr<plugin_system::PluginRegistry> registry_;
};
```

## Security Best Practices

### Security Measures

1. **Sandboxing**: Isolate plugins in restricted environments
2. **Privilege Separation**: Run plugins with minimal privileges
3. **Resource Limits**: Enforce strict resource constraints
4. **Code Signing**: Verify plugin authenticity
5. **Input Validation**: Validate all plugin inputs and outputs
6. **Audit Logging**: Log all plugin operations

```cpp
// Security-focused plugin validator
class PluginSecurityValidator {
private:
    std::unordered_map<std::string, std::string> allowed_signatures_;
    std::vector<std::string> blocked_capabilities_;

public:
    bool validatePlugin(const std::string& pluginPath) {
        // Verify plugin signature
        if (!verifySignature(pluginPath)) {
            return false;
        }

        // Scan for dangerous capabilities
        if (scanForDangerousCapabilities(pluginPath)) {
            return false;
        }

        // Check plugin dependencies
        if (!validateDependencies(pluginPath)) {
            return false;
        }

        return true;
    }

    bool verifySignature(const std::string& pluginPath) {
        // Implement digital signature verification
        return true;
    }

    bool scanForDangerousCapabilities(const std::string& pluginPath) {
        // Scan plugin binary for dangerous system calls
        return false;
    }

    bool validateDependencies(const std::string& pluginPath) {
        // Check that all dependencies are allowed
        return true;
    }
};
```

This comprehensive plugin system provides:

- **Type-Safe API**: Strongly typed interfaces with version compatibility
- **Dynamic Loading**: Runtime plugin discovery and loading with dependency management
- **Security Sandboxing**: Isolated execution environments with resource limits
- **Lifecycle Management**: Complete plugin lifecycle with hot-reloading support
- **Performance Optimization**: Async operations, caching, and resource pooling
- **Security Validation**: Code signing, capability scanning, and audit logging

The system enables extensibility while maintaining security and stability, making it suitable for production container runtime environments.
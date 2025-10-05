#pragma once

#include <docker-cpp/core/error.hpp>
#include <docker-cpp/plugin/plugin_interface.hpp>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace docker_cpp {

class PluginRegistry {
public:
    PluginRegistry() = default;
    ~PluginRegistry() = default;

    // Non-copyable, movable
    PluginRegistry(const PluginRegistry&) = delete;
    PluginRegistry& operator=(const PluginRegistry&) = delete;
    PluginRegistry(PluginRegistry&& other) noexcept;
    PluginRegistry& operator=(PluginRegistry&& other) noexcept;

    // Plugin registration and management
    void registerPlugin(const std::string& name, std::unique_ptr<IPlugin> plugin);
    void unregisterPlugin(const std::string& name);

    template <typename PluginType = IPlugin>
    PluginType* getPlugin(const std::string& name) const;

    std::vector<std::string> getPluginNames() const;
    bool hasPlugin(const std::string& name) const;

    // Plugin lifecycle management
    bool initializePlugin(const std::string& name, const PluginConfig& config = {});
    bool shutdownPlugin(const std::string& name);

    std::unordered_map<std::string, bool> initializeAllPlugins(const PluginConfig& config = {});
    void shutdownAllPlugins();

    // Plugin information
    PluginInfo getPluginInfo(const std::string& name) const;
    std::vector<PluginInfo> getAllPluginInfo() const;

    // Dependency management
    std::vector<std::string> getLoadOrder() const;
    bool validateDependencies(const std::string& plugin_name) const;
    std::unordered_map<std::string, std::vector<std::string>> getDependencyGraph() const;

    // Plugin discovery
    void loadPluginsFromDirectory(const std::string& plugin_dir);
    void setPluginLoader(std::function<std::unique_ptr<IPlugin>(const std::string&)> loader);

    // Statistics
    size_t getPluginCount() const;
    size_t getInitializedPluginCount() const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::unique_ptr<IPlugin>> plugins_;
    std::function<std::unique_ptr<IPlugin>(const std::string&)> plugin_loader_;

    // Helper methods
    void validatePluginName(const std::string& name) const;
    void validatePluginUniqueness(const std::string& name) const;
    void checkPluginExists(const std::string& name) const;
    bool isDependencySatisfied(const std::string& dependency) const;

    // Unsafe methods (assume mutex is already locked)
    std::vector<std::string> getLoadOrderUnsafe() const;
    bool initializePluginUnsafe(const std::string& name, const PluginConfig& config);
    bool shutdownPluginUnsafe(const std::string& name);
    bool validateDependenciesUnsafe(const std::string& plugin_name) const;
};

// Template implementation
template <typename PluginType>
PluginType* PluginRegistry::getPlugin(const std::string& name) const
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = plugins_.find(name);
    if (it == plugins_.end()) {
        return nullptr;
    }

    // Note: Using static_cast instead of dynamic_cast due to -fno-rtti flag
    // Caller must ensure the plugin is of the correct type
    return static_cast<PluginType*>(it->second.get());
}

} // namespace docker_cpp
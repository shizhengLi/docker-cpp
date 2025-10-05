#include <algorithm>
#include <docker-cpp/plugin/plugin_registry.hpp>
#include <filesystem>
#include <iostream>

namespace docker_cpp {

PluginRegistry::PluginRegistry(PluginRegistry&& other) noexcept
{
    std::lock_guard<std::mutex> lock(other.mutex_);
    plugins_ = std::move(other.plugins_);
    plugin_loader_ = std::move(other.plugin_loader_);
}

PluginRegistry& PluginRegistry::operator=(PluginRegistry&& other) noexcept
{
    if (this != &other) {
        std::lock(mutex_, other.mutex_);
        std::lock_guard<std::mutex> lock1(mutex_, std::adopt_lock);
        std::lock_guard<std::mutex> lock2(other.mutex_, std::adopt_lock);

        plugins_ = std::move(other.plugins_);
        plugin_loader_ = std::move(other.plugin_loader_);
    }
    return *this;
}

void PluginRegistry::registerPlugin(const std::string& name, std::unique_ptr<IPlugin> plugin)
{
    std::lock_guard<std::mutex> lock(mutex_);

    validatePluginName(name);
    validatePluginUniqueness(name);

    if (!plugin) {
        throw ContainerError(ErrorCode::INVALID_PLUGIN, "Cannot register null plugin: " + name);
    }

    plugins_[name] = std::move(plugin);
}

void PluginRegistry::unregisterPlugin(const std::string& name)
{
    std::lock_guard<std::mutex> lock(mutex_);

    checkPluginExists(name);

    // Shutdown plugin before removing
    auto it = plugins_.find(name);
    if (it->second->isInitialized()) {
        it->second->shutdown();
    }

    plugins_.erase(it);
}

std::vector<std::string> PluginRegistry::getPluginNames() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::string> names;
    names.reserve(plugins_.size());

    for (const auto& [name, plugin] : plugins_) {
        names.push_back(name);
    }

    return names;
}

bool PluginRegistry::hasPlugin(const std::string& name) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return plugins_.find(name) != plugins_.end();
}

bool PluginRegistry::initializePlugin(const std::string& name, const PluginConfig& config)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = plugins_.find(name);
    if (it == plugins_.end()) {
        return false;
    }

    // Check if plugin is already initialized
    if (it->second->isInitialized()) {
        return true;
    }

    // Validate dependencies before initialization
    if (!validateDependencies(name)) {
        return false;
    }

    try {
        return it->second->initialize(config);
    }
    catch (const std::exception& e) {
        // Log error and return false
        std::cerr << "Failed to initialize plugin '" << name << "': " << e.what() << std::endl;
        return false;
    }
}

bool PluginRegistry::shutdownPlugin(const std::string& name)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = plugins_.find(name);
    if (it == plugins_.end()) {
        return false;
    }

    if (!it->second->isInitialized()) {
        return true;
    }

    try {
        it->second->shutdown();
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "Failed to shutdown plugin '" << name << "': " << e.what() << std::endl;
        return false;
    }
}

std::unordered_map<std::string, bool>
PluginRegistry::initializeAllPlugins(const PluginConfig& config)
{
    std::lock_guard<std::mutex> lock(mutex_);

    std::unordered_map<std::string, bool> results;
    auto load_order = getLoadOrderUnsafe();

    for (const auto& name : load_order) {
        results[name] = initializePluginUnsafe(name, config);
    }

    return results;
}

void PluginRegistry::shutdownAllPlugins()
{
    std::lock_guard<std::mutex> lock(mutex_);

    // Shutdown in reverse order of initialization
    auto load_order = getLoadOrderUnsafe();
    std::reverse(load_order.begin(), load_order.end());

    for (const auto& name : load_order) {
        shutdownPluginUnsafe(name);
    }
}

PluginInfo PluginRegistry::getPluginInfo(const std::string& name) const
{
    std::lock_guard<std::mutex> lock(mutex_);

    checkPluginExists(name);

    auto it = plugins_.find(name);
    return it->second->getPluginInfo();
}

std::vector<PluginInfo> PluginRegistry::getAllPluginInfo() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<PluginInfo> infos;
    infos.reserve(plugins_.size());

    for (const auto& [name, plugin] : plugins_) {
        infos.push_back(plugin->getPluginInfo());
    }

    return infos;
}

std::vector<std::string> PluginRegistry::getLoadOrder() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::string> result;
    std::unordered_map<std::string, bool> visited;
    std::unordered_map<std::string, bool> visiting;

    // DFS-based topological sort
    std::function<void(const std::string&)> visit = [&](const std::string& name) {
        if (visiting[name]) {
            throw ContainerError(ErrorCode::CIRCULAR_DEPENDENCY,
                                 "Circular dependency detected involving plugin: " + name);
        }

        if (visited[name]) {
            return;
        }

        visiting[name] = true;

        auto it = plugins_.find(name);
        if (it != plugins_.end()) {
            for (const auto& dep : it->second->getDependencies()) {
                if (hasPlugin(dep)) {
                    visit(dep);
                }
            }
        }

        visiting[name] = false;
        visited[name] = true;
        result.push_back(name);
    };

    for (const auto& [name, plugin] : plugins_) {
        if (!visited[name]) {
            visit(name);
        }
    }

    return result;
}

bool PluginRegistry::validateDependencies(const std::string& plugin_name) const
{
    auto it = plugins_.find(plugin_name);
    if (it == plugins_.end()) {
        return false;
    }

    for (const auto& dependency : it->second->getDependencies()) {
        if (!isDependencySatisfied(dependency)) {
            return false;
        }
    }

    return true;
}

std::unordered_map<std::string, std::vector<std::string>> PluginRegistry::getDependencyGraph() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    std::unordered_map<std::string, std::vector<std::string>> graph;

    for (const auto& [name, plugin] : plugins_) {
        graph[name] = plugin->getDependencies();
    }

    return graph;
}

void PluginRegistry::loadPluginsFromDirectory(const std::string& plugin_dir)
{
    if (!std::filesystem::exists(plugin_dir)) {
        throw ContainerError(ErrorCode::DIRECTORY_NOT_FOUND,
                             "Plugin directory does not exist: " + plugin_dir);
    }

    if (!plugin_loader_) {
        throw ContainerError(ErrorCode::PLUGIN_LOADER_NOT_SET,
                             "Plugin loader not set. Use setPluginLoader() first.");
    }

    for (const auto& entry : std::filesystem::directory_iterator(plugin_dir)) {
        if (entry.is_regular_file()) {
            try {
                auto plugin = plugin_loader_(entry.path().string());
                if (plugin) {
                    registerPlugin(plugin->getName(), std::move(plugin));
                }
            }
            catch (const std::exception& e) {
                std::cerr << "Failed to load plugin from '" << entry.path() << "': " << e.what()
                          << std::endl;
            }
        }
    }
}

void PluginRegistry::setPluginLoader(
    std::function<std::unique_ptr<IPlugin>(const std::string&)> loader)
{
    plugin_loader_ = std::move(loader);
}

size_t PluginRegistry::getPluginCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return plugins_.size();
}

size_t PluginRegistry::getInitializedPluginCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    size_t count = 0;
    for (const auto& [name, plugin] : plugins_) {
        if (plugin->isInitialized()) {
            ++count;
        }
    }

    return count;
}

// Private helper methods
void PluginRegistry::validatePluginName(const std::string& name) const
{
    if (name.empty()) {
        throw ContainerError(ErrorCode::INVALID_PLUGIN_NAME, "Plugin name cannot be empty");
    }

    // Check for invalid characters
    if (name.find_first_of(" \t\n\r/") != std::string::npos) {
        throw ContainerError(ErrorCode::INVALID_PLUGIN_NAME,
                             "Plugin name contains invalid characters: " + name);
    }
}

void PluginRegistry::validatePluginUniqueness(const std::string& name) const
{
    if (plugins_.find(name) != plugins_.end()) {
        throw ContainerError(ErrorCode::DUPLICATE_PLUGIN,
                             "Plugin with name '" + name + "' is already registered");
    }
}

void PluginRegistry::checkPluginExists(const std::string& name) const
{
    if (plugins_.find(name) == plugins_.end()) {
        throw ContainerError(ErrorCode::PLUGIN_NOT_FOUND, "Plugin not found: " + name);
    }
}

bool PluginRegistry::isDependencySatisfied(const std::string& dependency) const
{
    auto it = plugins_.find(dependency);
    return it != plugins_.end() && it->second->isInitialized();
}

// Private unsafe methods (assume mutex is already locked)
std::vector<std::string> PluginRegistry::getLoadOrderUnsafe() const
{
    std::vector<std::string> result;
    std::unordered_map<std::string, bool> visited;
    std::unordered_map<std::string, bool> visiting;

    // DFS-based topological sort
    std::function<void(const std::string&)> visit = [&](const std::string& name) {
        if (visiting[name]) {
            throw ContainerError(ErrorCode::CIRCULAR_DEPENDENCY,
                                 "Circular dependency detected involving plugin: " + name);
        }

        if (visited[name]) {
            return;
        }

        visiting[name] = true;

        auto it = plugins_.find(name);
        if (it != plugins_.end()) {
            for (const auto& dep : it->second->getDependencies()) {
                if (plugins_.find(dep) != plugins_.end()) {
                    visit(dep);
                }
            }
        }

        visiting[name] = false;
        visited[name] = true;
        result.push_back(name);
    };

    for (const auto& [name, plugin] : plugins_) {
        if (!visited[name]) {
            visit(name);
        }
    }

    return result;
}

bool PluginRegistry::initializePluginUnsafe(const std::string& name, const PluginConfig& config)
{
    auto it = plugins_.find(name);
    if (it == plugins_.end()) {
        return false;
    }

    // Check if plugin is already initialized
    if (it->second->isInitialized()) {
        return true;
    }

    // Validate dependencies before initialization
    if (!validateDependenciesUnsafe(name)) {
        return false;
    }

    try {
        return it->second->initialize(config);
    }
    catch (const std::exception& e) {
        // Log error and return false
        std::cerr << "Failed to initialize plugin '" << name << "': " << e.what() << std::endl;
        return false;
    }
}

bool PluginRegistry::shutdownPluginUnsafe(const std::string& name)
{
    auto it = plugins_.find(name);
    if (it == plugins_.end()) {
        return false;
    }

    if (!it->second->isInitialized()) {
        return true;
    }

    try {
        it->second->shutdown();
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "Failed to shutdown plugin '" << name << "': " << e.what() << std::endl;
        return false;
    }
}

bool PluginRegistry::validateDependenciesUnsafe(const std::string& plugin_name) const
{
    auto it = plugins_.find(plugin_name);
    if (it == plugins_.end()) {
        return false;
    }

    for (const auto& dependency : it->second->getDependencies()) {
        if (!isDependencySatisfied(dependency)) {
            return false;
        }
    }

    return true;
}

} // namespace docker_cpp
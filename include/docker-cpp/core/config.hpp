#pragma once

#include <docker-cpp/core/error.hpp>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <variant>
#include <vector>

namespace docker_cpp {

using ConfigValue = std::variant<std::string, int, double, bool>;
using ConfigChangeCallback = std::function<void(const std::string&, const ConfigValue&, const ConfigValue&)>;
using ConfigLayer = std::unordered_map<std::string, ConfigValue>;

class ConfigManager {
public:
    ConfigManager() = default;
    ~ConfigManager() = default;

    // Basic configuration operations
    template <typename T>
    void set(const std::string& key, const T& value);

    template <typename T>
    T get(const std::string& key) const;

    template <typename T>
    T get(const std::string& key, const T& default_value) const;

    bool has(const std::string& key) const;
    void remove(const std::string& key);
    void clear();

    // Configuration layers
    void addLayer(const std::string& layer_name, const ConfigManager& other);
    void merge(const ConfigManager& other);
    ConfigManager getEffectiveConfig() const;

    // Environment variable expansion
    ConfigManager expandEnvironmentVariables() const;
    std::string expandValue(const std::string& value) const;

    // File operations
    void loadFromFile(const std::string& filename);
    void saveToFile(const std::string& filename) const;

    // Change notifications
    void addChangeListener(const std::string& key, ConfigChangeCallback callback);
    void removeChangeListener(const std::string& key);
    std::vector<std::tuple<std::string, ConfigValue, ConfigValue>> getRecentChanges() const;
  size_t getLayerCount() const { return layers_.size(); }

    // Copyable and movable
    ConfigManager(const ConfigManager&) = default;
    ConfigManager& operator=(const ConfigManager&) = default;
    ConfigManager(ConfigManager&&) = default;
    ConfigManager& operator=(ConfigManager&&) = default;

private:
    std::unordered_map<std::string, ConfigValue> config_data_;
    std::vector<std::pair<std::string, ConfigManager>> layers_;
    std::unordered_map<std::string, ConfigChangeCallback> change_listeners_;
    std::vector<std::tuple<std::string, ConfigValue, ConfigValue>> recent_changes_;

    ConfigValue getValue(const std::string& key) const;
    void setValue(const std::string& key, const ConfigValue& value);
    void notifyChange(const std::string& key, const ConfigValue& old_value, const ConfigValue& new_value);
    std::string expandEnvironmentVariable(const std::string& str) const;
};

// Template implementations
template <typename T>
void ConfigManager::set(const std::string& key, const T& value) {
    ConfigValue old_value;
    bool has_old = false;

    if (config_data_.find(key) != config_data_.end()) {
        old_value = config_data_[key];
        has_old = true;
    }

    config_data_[key] = value;

    if (has_old) {
        notifyChange(key, old_value, value);
    }
}

template <typename T>
T ConfigManager::get(const std::string& key) const {
    auto value = getValue(key);
    try {
        return std::get<T>(value);
    } catch (const std::bad_variant_access&) {
        throw ContainerError(ErrorCode::INVALID_TYPE, "Invalid type for configuration key: " + key);
    }
}

template <typename T>
T ConfigManager::get(const std::string& key, const T& default_value) const {
    if (!has(key)) {
        return default_value;
    }
    return get<T>(key);
}

} // namespace docker_cpp
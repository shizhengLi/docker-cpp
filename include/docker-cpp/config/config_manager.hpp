#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <variant>
#include <filesystem>
#include <functional>
#include <memory>
#include <docker-cpp/core/error.hpp>

namespace docker_cpp {

// Configuration value type enumeration
enum class ConfigValueType {
    STRING,
    INTEGER,
    BOOLEAN,
    DOUBLE,
    ARRAY,
    OBJECT
};

// Configuration value wrapper
class ConfigValue {
public:
    using VariantType = std::variant<std::string, int, bool, double>;

    ConfigValue() = default;

    template<typename T>
    ConfigValue(T value) : value_(std::move(value)) {}

    template<typename T>
    T get() const {
        try {
            return std::get<T>(value_);
        } catch (const std::bad_variant_access&) {
            throw ContainerError(ErrorCode::CONFIG_INVALID,
                               "Type mismatch in configuration value access");
        }
    }

    ConfigValueType getType() const {
        return static_cast<ConfigValueType>(value_.index());
    }

    std::string toString() const;

    VariantType value_;
};

// Configuration validation schema
using ConfigSchema = std::unordered_map<std::string, ConfigValueType>;

// Configuration change callback
using ConfigChangeCallback = std::function<void(const std::string& key, const ConfigValue& old_value, const ConfigValue& new_value)>;

class ConfigManager {
public:
    ConfigManager() = default;
    ~ConfigManager() = default;

    // Not copyable due to unique_ptr members
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    // Movable
    ConfigManager(ConfigManager&& other) noexcept = default;
    ConfigManager& operator=(ConfigManager&& other) noexcept = default;

    // Value access methods
    template<typename T>
    void set(const std::string& key, T value);

    template<typename T>
    T get(const std::string& key) const;

    template<typename T>
    T get(const std::string& key, T default_value) const;

    bool has(const std::string& key) const;
    void remove(const std::string& key);
    void clear();

    // Configuration state
    bool isEmpty() const;
    size_t size() const;

    // Key operations
    std::vector<std::string> getKeys() const;
    std::vector<std::string> getKeysWithPrefix(const std::string& prefix) const;

    // Nested configuration
    ConfigManager getSubConfig(const std::string& prefix) const;

    // File operations
    void loadFromJsonFile(const std::filesystem::path& file_path);
    void loadFromJsonString(const std::string& json_string);
    void saveToJsonFile(const std::filesystem::path& file_path) const;
    std::string toJsonString() const;

    // Configuration merging
    void merge(const ConfigManager& other);
    void mergeFromJsonString(const std::string& json_string);

    // Environment variable expansion
    ConfigManager expandEnvironmentVariables() const;

    // Validation
    void validate(const ConfigSchema& schema) const;

    // Change notifications
    void setChangeCallback(ConfigChangeCallback callback);
    void enableChangeNotifications(bool enabled);

    // Configuration layers (for overrides)
    void addLayer(const std::string& name, const ConfigManager& layer);
    void removeLayer(const std::string& name);
    ConfigManager getEffectiveConfig() const;

    // Watch for file changes
    void watchFile(const std::filesystem::path& file_path);
    void stopWatching();

private:
    std::unordered_map<std::string, ConfigValue> values_;
    std::unordered_map<std::string, std::unique_ptr<ConfigManager>> layers_;
    ConfigChangeCallback change_callback_;
    bool change_notifications_enabled_ = false;
    std::string watched_file_;

    // Helper methods
    std::vector<std::string> splitKey(const std::string& key) const;
    std::string joinKey(const std::vector<std::string>& parts) const;
    void notifyChange(const std::string& key, const ConfigValue& old_value, const ConfigValue& new_value);
    ConfigValue getEffectiveValue(const std::string& key) const;
    void parseJsonValue(const std::string& key, const std::string& json_value);
    std::string serializeToJson() const;
    std::string expandValue(const std::string& value) const;
    ConfigManager copyValuesOnly() const;
};

// Template implementations
template<typename T>
void ConfigManager::set(const std::string& key, T value) {
    ConfigValue old_value;
    bool had_old_value = false;

    if (has(key)) {
        old_value = values_.at(key);
        had_old_value = true;
    }

    values_[key] = ConfigValue(std::move(value));

    if (change_notifications_enabled_ && had_old_value && change_callback_) {
        notifyChange(key, old_value, values_[key]);
    }
}

template<typename T>
T ConfigManager::get(const std::string& key) const {
    if (!has(key)) {
        throw ContainerError(ErrorCode::CONFIG_MISSING,
                           "Configuration key not found: " + key);
    }
    return getEffectiveValue(key).get<T>();
}

template<typename T>
T ConfigManager::get(const std::string& key, T default_value) const {
    if (!has(key)) {
        return default_value;
    }
    return getEffectiveValue(key).get<T>();
}

} // namespace docker_cpp
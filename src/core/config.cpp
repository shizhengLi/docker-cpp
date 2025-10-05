#include <algorithm>
#include <cstdlib>
#include <docker-cpp/core/config.hpp>
#include <docker-cpp/core/error.hpp>
#include <fstream>
#include <iostream>
#include <sstream>

namespace docker_cpp {

ConfigValue ConfigManager::getValue(const std::string& key) const
{
    // Check current config first (highest priority)
    auto it = config_data_.find(key);
    if (it != config_data_.end()) {
        return it->second;
    }

    // Check layers in reverse order (last layer has higher priority than earlier layers)
    for (auto layer_it = layers_.rbegin(); layer_it != layers_.rend(); ++layer_it) {
        if (layer_it->second.has(key)) {
            return layer_it->second.config_data_.at(key);
        }
    }

    throw ContainerError(ErrorCode::CONFIG_MISSING, "Configuration key not found: " + key);
}

void ConfigManager::setValue(const std::string& key, const ConfigValue& value)
{
    config_data_[key] = value;
}

bool ConfigManager::has(const std::string& key) const
{
    // Check current config first
    if (config_data_.find(key) != config_data_.end()) {
        return true;
    }

    // Check layers (avoid recursion by checking config_data directly)
    for (const auto& layer : layers_) {
        if (layer.second.config_data_.find(key) != layer.second.config_data_.end()) {
            return true;
        }
    }

    return false;
}

void ConfigManager::remove(const std::string& key)
{
    auto it = config_data_.find(key);
    if (it != config_data_.end()) {
        ConfigValue old_value = it->second;
        config_data_.erase(it);
        notifyChange(key, old_value, ConfigValue{});
    }
}

void ConfigManager::clear()
{
    config_data_.clear();
    layers_.clear();
    change_listeners_.clear();
    recent_changes_.clear();
}

void ConfigManager::addLayer(const std::string& layer_name, const ConfigManager& other)
{
    layers_.emplace_back(layer_name, other);
}

void ConfigManager::merge(const ConfigManager& other)
{
    for (const auto& [key, value] : other.config_data_) {
        setValue(key, value);
    }
}

ConfigManager ConfigManager::getEffectiveConfig() const
{
    ConfigManager result;

    // Start with current config (this is the merged base config)
    result.config_data_ = config_data_;

    // Apply layers in order they were added - later layers override earlier ones
    for (const auto& [layer_name, layer_config] : layers_) {
        for (const auto& [key, value] : layer_config.config_data_) {
            result.config_data_[key] = value;
        }
    }

    return result;
}

ConfigManager ConfigManager::expandEnvironmentVariables() const
{
    ConfigManager result = *this;

    for (auto& [key, value] : result.config_data_) {
        if (std::holds_alternative<std::string>(value)) {
            std::string expanded = expandValue(std::get<std::string>(value));
            result.setValue(key, expanded);
        }
    }

    return result;
}

std::string ConfigManager::expandValue(const std::string& value) const
{
    return expandEnvironmentVariable(value);
}

void ConfigManager::loadFromFile(const std::string& filename)
{
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw ContainerError(ErrorCode::FILE_NOT_FOUND, "Cannot open config file: " + filename);
    }

    std::string line;
    while (std::getline(file, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }

        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) {
            continue; // Skip malformed lines
        }

        std::string key = line.substr(0, eq_pos);
        std::string value = line.substr(eq_pos + 1);

        // Remove whitespace
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);

        setValue(key, value);
    }
}

void ConfigManager::saveToFile(const std::string& filename) const
{
    std::ofstream file(filename);
    if (!file.is_open()) {
        throw ContainerError(ErrorCode::FILE_NOT_FOUND, "Cannot create config file: " + filename);
    }

    for (const auto& [key, value] : config_data_) {
        file << key << " = ";

        if (std::holds_alternative<std::string>(value)) {
            file << std::get<std::string>(value);
        }
        else if (std::holds_alternative<int>(value)) {
            file << std::get<int>(value);
        }
        else if (std::holds_alternative<double>(value)) {
            file << std::get<double>(value);
        }
        else if (std::holds_alternative<bool>(value)) {
            file << (std::get<bool>(value) ? "true" : "false");
        }

        file << "\n";
    }
}

void ConfigManager::addChangeListener(const std::string& key, ConfigChangeCallback callback)
{
    change_listeners_[key] = callback;
}

void ConfigManager::removeChangeListener(const std::string& key)
{
    change_listeners_.erase(key);
}

std::vector<std::tuple<std::string, ConfigValue, ConfigValue>>
ConfigManager::getRecentChanges() const
{
    return recent_changes_;
}

void ConfigManager::notifyChange(const std::string& key,
                                 const ConfigValue& old_value,
                                 const ConfigValue& new_value)
{
    recent_changes_.emplace_back(key, old_value, new_value);

    auto it = change_listeners_.find(key);
    if (it != change_listeners_.end()) {
        it->second(key, old_value, new_value);
    }
}

std::string ConfigManager::expandEnvironmentVariable(const std::string& str) const
{
    std::string result = str;
    size_t start = 0;

    while ((start = result.find("${", start)) != std::string::npos) {
        size_t end = result.find('}', start);
        if (end == std::string::npos) {
            break; // Malformed input
        }

        std::string var_name = result.substr(start + 2, end - start - 2);
        const char* env_value = std::getenv(var_name.c_str());

        if (env_value) {
            std::string replacement = env_value;
            result.replace(start, end - start + 1, replacement);
            start += replacement.length();
        }
        else {
            // Environment variable not found, leave pattern unchanged
            start = end + 1;
        }
    }

    return result;
}

} // namespace docker_cpp
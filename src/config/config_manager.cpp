#include <algorithm>
#include <cstdlib>
#include <docker-cpp/config/config_manager.hpp>
#include <fstream>
#include <regex>
#include <sstream>

// Include nlohmann/json if available
// For static analysis tools like cppcheck, we define it manually
#ifdef CPPCHECK
    #define HAS_NLOHMANN_JSON 0
#else
    // Try to detect nlohmann/json availability
    #if defined(__has_include)
        #if __has_include(<nlohmann/json.hpp>)
            #include <nlohmann/json.hpp>
            #define HAS_NLOHMANN_JSON 1
        #else
            #define HAS_NLOHMANN_JSON 0
        #endif
    #else
        #define HAS_NLOHMANN_JSON 0
    #endif
#endif

#if !HAS_NLOHMANN_JSON
// Simple JSON parser fallback when nlohmann::json is not available
namespace {
std::string simpleJsonEscape(const std::string& str)
{
    std::string result;
    for (char c : str) {
        switch (c) {
            case '"':
                result += "\\\"";
                break;
            case '\\':
                result += "\\\\";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            default:
                result += c;
                break;
        }
    }
    return result;
}
} // namespace
#endif

namespace docker_cpp {

// Copy constructor
ConfigManager::ConfigManager(const ConfigManager& other)
    : values_(other.values_),
      change_callback_(other.change_callback_),
      change_notifications_enabled_(other.change_notifications_enabled_),
      watched_file_(other.watched_file_)
{
    // Deep copy layers
    for (const auto& [name, layer] : other.layers_) {
        layers_[name] = std::make_unique<ConfigManager>(*layer);
    }
}

// Copy assignment operator
ConfigManager& ConfigManager::operator=(const ConfigManager& other)
{
    if (this != &other) {
        std::lock_guard<std::recursive_mutex> lock(values_mutex_);
        values_ = other.values_;
        change_callback_ = other.change_callback_;
        change_notifications_enabled_ = other.change_notifications_enabled_;
        watched_file_ = other.watched_file_;

        // Deep copy layers
        layers_.clear();
        for (const auto& [name, layer] : other.layers_) {
            layers_[name] = std::make_unique<ConfigManager>(*layer);
        }
    }
    return *this;
}

// Move constructor
ConfigManager::ConfigManager(ConfigManager&& other) noexcept
    : values_(std::move(other.values_)),
      layers_(std::move(other.layers_)),
      change_callback_(std::move(other.change_callback_)),
      change_notifications_enabled_(other.change_notifications_enabled_),
      watched_file_(std::move(other.watched_file_))
{
}

// Move assignment operator
ConfigManager& ConfigManager::operator=(ConfigManager&& other) noexcept
{
    if (this != &other) {
        std::lock_guard<std::recursive_mutex> lock(values_mutex_);
        values_ = std::move(other.values_);
        layers_ = std::move(other.layers_);
        change_callback_ = std::move(other.change_callback_);
        change_notifications_enabled_ = other.change_notifications_enabled_;
        watched_file_ = std::move(other.watched_file_);
    }
    return *this;
}

#if HAS_NLOHMANN_JSON
namespace {
// Helper function to process JSON arrays efficiently
std::string processJsonArray(const nlohmann::json& j)
{
    std::string array_str;
    size_t total_size = 0;
    std::vector<std::string> items;

    // First pass: collect items and calculate total size
    for (const auto& item : j) {
        std::string item_str;
        if (item.is_string()) {
            item_str = item.get<std::string>();
        }
        else {
            item_str = item.dump();
        }
        items.push_back(std::move(item_str));
        total_size += items.back().length();
    }

    // Reserve space and concatenate efficiently
    if (!items.empty()) {
        array_str.reserve(total_size + items.size() - 1); // + commas
        for (size_t i = 0; i < items.size(); ++i) {
            if (i > 0) {
                array_str += ",";
            }
            array_str += items[i];
        }
    }
    return array_str;
}
} // namespace
#endif

std::string ConfigValue::toString() const
{
    if (value_.index() == 0 && std::get<std::string>(value_).empty()) {
        return "";
    }

    switch (getType()) {
        case ConfigValueType::STRING:
            return get<std::string>();
        case ConfigValueType::INTEGER:
            return std::to_string(get<int>());
        case ConfigValueType::BOOLEAN:
            return get<bool>() ? "true" : "false";
        case ConfigValueType::DOUBLE:
            return std::to_string(get<double>());
        default:
            return "";
    }
}

// ConfigManager implementation
bool ConfigManager::isEmpty() const
{
    return values_.empty() && layers_.empty();
}

size_t ConfigManager::size() const // NOLINT(misc-no-recursion)
{
    size_t count = values_.size();
    for (const auto& [unused_name, layer] : layers_) {
        count += layer->size(); // NOLINT(misc-no-recursion)
    }
    return count;
}

bool ConfigManager::has(const std::string& key) const // NOLINT(misc-no-recursion)
{
    std::lock_guard<std::recursive_mutex> lock(values_mutex_);

    if (values_.find(key) != values_.end()) {
        return true;
    }

    // Check layers
    for (const auto& [unused_name, layer] : layers_) {
        if (layer->has(key)) { // NOLINT(misc-no-recursion)
            return true;
        }
    }

    return false;
}

void ConfigManager::remove(const std::string& key)
{
    std::lock_guard<std::recursive_mutex> lock(values_mutex_);

    ConfigValue old_value;
    bool had_old_value = false;

    if (values_.find(key) != values_.end()) {
        old_value = values_[key];
        had_old_value = true;
    }

    if (values_.erase(key) > 0 && change_notifications_enabled_ && change_callback_) {
        // Notify removal with empty value
        notifyChange(key, old_value, ConfigValue{});
    }
}

void ConfigManager::clear()
{
    std::lock_guard<std::recursive_mutex> lock(values_mutex_);
    values_.clear();
    layers_.clear();
}

std::vector<std::string> ConfigManager::getKeys() const // NOLINT(misc-no-recursion)
{
    std::vector<std::string> keys;
    keys.reserve(values_.size() + 20); // Reserve space for estimated keys

    // Add keys from this config
    for (const auto& [key, value] : values_) {
        keys.push_back(key);
    }

    // Add keys from layers
    for (const auto& [unused_name, layer] : layers_) {
        auto layer_keys = layer->getKeys(); // NOLINT(misc-no-recursion)
        keys.insert(keys.end(), layer_keys.begin(), layer_keys.end());
    }

    // Remove duplicates and sort
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());

    return keys;
}

std::vector<std::string>
ConfigManager::getKeysWithPrefix(const std::string& prefix) const // NOLINT(misc-no-recursion)
{
    std::vector<std::string> keys;

    // Add matching keys from this config
    for (const auto& [key, value] : values_) {
        if (key.substr(0, prefix.length()) == prefix) {
            keys.push_back(key);
        }
    }

    // Add matching keys from layers
    for (const auto& [unused_name, layer] : layers_) {
        auto layer_keys = layer->getKeysWithPrefix(prefix); // NOLINT(misc-no-recursion)
        keys.insert(keys.end(), layer_keys.begin(), layer_keys.end());
    }

    // Remove duplicates and sort
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());

    return keys;
}

ConfigManager
ConfigManager::getSubConfig(const std::string& prefix) const // NOLINT(misc-no-recursion)
{
    ConfigManager sub_config;

    // Copy matching keys from this config
    for (const auto& [key, value] : values_) {
        if (key.substr(0, prefix.length()) == prefix) {
            std::string sub_key = key.substr(prefix.length());
            sub_config.values_[sub_key] = value;
        }
    }

    // Copy matching keys from layers
    for (const auto& [unused_name, layer] : layers_) {
        auto layer_sub = layer->getSubConfig(prefix); // NOLINT(misc-no-recursion)
        sub_config.merge(layer_sub);
    }

    return sub_config;
}

void ConfigManager::loadFromJsonFile(const std::filesystem::path& file_path)
{
    if (!std::filesystem::exists(file_path)) {
        throw ContainerError(ErrorCode::CONFIG_MISSING,
                             "Configuration file not found: " + file_path.string());
    }

    std::ifstream file(file_path);
    if (!file.is_open()) {
        throw ContainerError(ErrorCode::IO_ERROR,
                             "Failed to open configuration file: " + file_path.string());
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    loadFromJsonString(buffer.str());
}

void ConfigManager::loadFromJsonString(const std::string& json_string)
{
    clear();
    mergeFromJsonString(json_string);
}

void ConfigManager::saveToJsonFile(const std::filesystem::path& file_path) const
{
    std::ofstream file(file_path);
    if (!file.is_open()) {
        throw ContainerError(ErrorCode::IO_ERROR,
                             "Failed to create configuration file: " + file_path.string());
    }

    file << toJsonString();
    file.close();
}

std::string ConfigManager::toJsonString() const
{
    return serializeToJson();
}

void ConfigManager::merge(const ConfigManager& other)
{
    for (const auto& [key, value] : other.values_) {
        set(key, value);
    }

    // Merge layers
    for (const auto& [name, layer] : other.layers_) {
        layers_.emplace(name, std::make_unique<ConfigManager>(layer->copyValuesOnly()));
    }
}

void ConfigManager::mergeFromJsonString(const std::string& json_string)
{
#if HAS_NLOHMANN_JSON
    try {
        nlohmann::json json = nlohmann::json::parse(json_string);

        std::function<void(const nlohmann::json&, const std::string&)> process_json =
            [&](const nlohmann::json& j, const std::string& prefix) {
                if (j.is_object()) {
                    for (auto& [key, value] : j.items()) {
                        std::string full_key;
                        if (prefix.empty()) {
                            full_key = key;
                        }
                        else {
                            full_key.reserve(prefix.length() + 1 + key.length());
                            full_key = prefix;
                            full_key += ".";
                            full_key += key;
                        }
                        process_json(value, full_key);
                    }
                }
                else if (j.is_string()) {
                    set(prefix, j.get<std::string>());
                }
                else if (j.is_number_integer()) {
                    set(prefix, j.get<int>());
                }
                else if (j.is_number_float()) {
                    set(prefix, j.get<double>());
                }
                else if (j.is_boolean()) {
                    set(prefix, j.get<bool>());
                }
                else if (j.is_array()) {
                    set(prefix, processJsonArray(j));
                }
            };

        process_json(json, "");
    }
    catch (const nlohmann::json::parse_error& e) {
        throw ContainerError(ErrorCode::CONFIG_INVALID,
                             "Invalid JSON configuration: " + std::string(e.what()));
    }
#else
    // Simple JSON parsing fallback
    // This is a very basic implementation - in production, use a proper JSON library
    try {
        // Remove whitespace for basic validation
        std::string cleaned = json_string;
        std::regex whitespace(R"(\s+)");
        cleaned = std::regex_replace(cleaned, whitespace, "");

        // Basic validation: check for obvious syntax errors
        if (cleaned.empty() || (cleaned.front() != '{' && cleaned.front() != '[')
            || (cleaned.back() != '}' && cleaned.back() != ']')) {
            throw ContainerError(ErrorCode::CONFIG_INVALID,
                                 "Invalid JSON: must start with { or [ and end with } or ]");
        }

        // Check for unbalanced quotes
        size_t quote_count = 0;
        bool in_string = false;
        for (size_t i = 0; i < cleaned.length(); ++i) {
            if (cleaned[i] == '"' && (i == 0 || cleaned[i-1] != '\\')) {
                quote_count++;
                in_string = !in_string;
            }
        }
        if (quote_count % 2 != 0) {
            throw ContainerError(ErrorCode::CONFIG_INVALID, "Invalid JSON: unbalanced quotes");
        }

        // Check for obviously malformed patterns (dangling colons, etc.)
        if (cleaned.find(":}") != std::string::npos || cleaned.find(":]") != std::string::npos) {
            throw ContainerError(ErrorCode::CONFIG_INVALID, "Invalid JSON: malformed structure");
        }

        std::regex kv_pattern(
            R"(\"([^\"]+)\"\s*:\s*\"([^\"]*)\"|\"([^\"]+)\"\s*:\s*(\d+)|\"([^\"]+)\"\s*:\s*(true|false))");
        std::sregex_iterator iter(cleaned.begin(), cleaned.end(), kv_pattern);
        std::sregex_iterator end;

        for (; iter != end; ++iter) {
            std::smatch match = *iter;

            if (match[2].matched) { // String value
                set(match[1].str(), match[2].str());
            }
            else if (match[4].matched) { // Integer value
                set(match[3].str(), std::stoi(match[4].str()));
            }
            else if (match[6].matched) { // Boolean value
                set(match[5].str(), match[6].str() == "true");
            }
        }
    }
    catch (const ContainerError&) {
        throw; // Re-throw ContainerError as-is
    }
    catch (const std::exception& e) {
        throw ContainerError(ErrorCode::CONFIG_INVALID,
                             "JSON parsing failed: " + std::string(e.what()));
    }
#endif
}

ConfigManager ConfigManager::expandEnvironmentVariables() const // NOLINT(misc-no-recursion)
{
    ConfigManager expanded;

    for (const auto& [key, value] : values_) {
        if (value.getType() == ConfigValueType::STRING) {
            std::string expanded_value = expandValue(value.toString());
            expanded.set(key, expanded_value);
        }
        else {
            expanded.set(key, value);
        }
    }

    // Expand layers
    for (const auto& [name, layer] : layers_) {
        expanded.addLayer(name, layer->expandEnvironmentVariables()); // NOLINT(misc-no-recursion)
    }

    return expanded;
}

void ConfigManager::validate(const ConfigSchema& schema) const
{
    for (const auto& [key, expected_type] : schema) {
        if (has(key)) {
            ConfigValue value = getEffectiveValue(key);
            if (value.getType() != expected_type) {
                throw ContainerError(ErrorCode::CONFIG_INVALID,
                                     "Type mismatch for configuration key '" + key + "'");
            }
        }
    }
}

void ConfigManager::setChangeCallback(ConfigChangeCallback callback)
{
    change_callback_ = std::move(callback);
}

void ConfigManager::enableChangeNotifications(bool enabled)
{
    change_notifications_enabled_ = enabled;
}

void ConfigManager::addLayer(const std::string& name, const ConfigManager& layer)
{
    auto new_layer = std::make_unique<ConfigManager>();
    new_layer->values_ = layer.values_;
    layers_.emplace(name, std::move(new_layer));
}

void ConfigManager::removeLayer(const std::string& name)
{
    layers_.erase(name);
}

ConfigManager ConfigManager::getEffectiveConfig() const
{
    ConfigManager effective;

    // Start with a copy of current values (base priority)
    effective.values_ = values_;

    // Apply layers in order - later layers override earlier ones
    for (const auto& [unused_name, layer] : layers_) {
        for (const auto& [key, value] : layer->values_) {
            effective.values_[key] = value;
        }
    }

    return effective;
}

void ConfigManager::watchFile(const std::filesystem::path& file_path)
{
    watched_file_ = file_path.string();
    // Note: File watching implementation would depend on platform-specific APIs
    // This is a placeholder for the concept
}

void ConfigManager::stopWatching()
{
    watched_file_.clear();
}

// Private helper methods
std::vector<std::string> ConfigManager::splitKey(const std::string& key) const
{
    std::vector<std::string> parts;
    std::stringstream ss(key);
    std::string part;

    while (std::getline(ss, part, '.')) {
        parts.push_back(part);
    }

    return parts;
}

std::string ConfigManager::joinKey(const std::vector<std::string>& parts) const
{
    std::string result;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0)
            result += ".";
        result += parts[i];
    }
    return result;
}

void ConfigManager::notifyChange(const std::string& key,
                                 const ConfigValue& old_value,
                                 const ConfigValue& new_value)
{
    if (change_callback_) {
        change_callback_(key, old_value, new_value);
    }
}

ConfigValue
ConfigManager::getEffectiveValue(const std::string& key) const // NOLINT(misc-no-recursion)
{
    std::lock_guard<std::recursive_mutex> lock(values_mutex_);

    // Check current config first (highest priority)
    auto it = values_.find(key);
    if (it != values_.end()) {
        return it->second;
    }

    // Check layers in order
    for (const auto& [unused_name, layer] : layers_) {
        if (layer->has(key)) {
            return layer->getEffectiveValue(key); // NOLINT(misc-no-recursion)
        }
    }

    throw ContainerError(ErrorCode::CONFIG_MISSING, "Configuration key not found: " + key);
}

std::string ConfigManager::serializeToJson() const
{
#if HAS_NLOHMANN_JSON
    nlohmann::json json;

    std::function<void(nlohmann::json&, const std::string&, const ConfigValue&)> add_value =
        [&](nlohmann::json& j, const std::string& key, const ConfigValue& value) {
            auto parts = splitKey(key);
            nlohmann::json* current = &j;

            for (size_t i = 0; i < parts.size() - 1; ++i) {
                if (!current->contains(parts[i])) {
                    (*current)[parts[i]] = nlohmann::json::object();
                }
                current = &(*current)[parts[i]];
            }

            switch (value.getType()) {
                case ConfigValueType::STRING:
                    (*current)[parts.back()] = value.get<std::string>();
                    break;
                case ConfigValueType::INTEGER:
                    (*current)[parts.back()] = value.get<int>();
                    break;
                case ConfigValueType::BOOLEAN:
                    (*current)[parts.back()] = value.get<bool>();
                    break;
                case ConfigValueType::DOUBLE:
                    (*current)[parts.back()] = value.get<double>();
                    break;
                default:
                    (*current)[parts.back()] = value.toString();
                    break;
            }
        };

    // Add all values
    for (const auto& [unused_key, value] : values_) {
        add_value(json, unused_key, value);
    }

    return json.dump(4); // NOLINT(readability-magic-numbers)
#else
    // Simple JSON serialization fallback
    std::stringstream ss;
    ss << "{\n";

    bool first = true;
    for (const auto& [key, value] : values_) {
        if (!first)
            ss << ",\n";
        first = false;

        ss << "  \"" << key << "\": ";

        switch (value.getType()) {
            case ConfigValueType::STRING:
                ss << "\"" << simpleJsonEscape(value.toString()) << "\"";
                break;
            case ConfigValueType::INTEGER:
                ss << value.get<int>();
                break;
            case ConfigValueType::BOOLEAN:
                ss << (value.get<bool>() ? "true" : "false");
                break;
            case ConfigValueType::DOUBLE:
                ss << value.get<double>();
                break;
            default:
                ss << "\"" << simpleJsonEscape(value.toString()) << "\"";
                break;
        }
    }

    ss << "\n}";
    return ss.str();
#endif
}

std::string ConfigManager::expandValue(const std::string& value) const
{
    std::string result = value;
    std::regex env_pattern(R"(\$\{([^}]+)\})");

    std::sregex_iterator iter(value.begin(), value.end(), env_pattern);
    std::sregex_iterator end;

    // Process replacements from back to front to avoid position issues
    struct Replacement {
        size_t position;
        size_t length;
        std::string replacement;

        Replacement(size_t pos, size_t len, std::string repl)
            : position(pos), length(len), replacement(std::move(repl))
        {}
    };
    std::vector<Replacement> replacements;

    for (; iter != end; ++iter) {
        const std::smatch& match = *iter;
        std::string var_name = match[1].str();

        // Note: std::getenv is not thread-safe, but this is acceptable for config loading
        // which typically happens during single-threaded initialization
        const char* env_value = std::getenv(var_name.c_str());
        if (env_value) {
            std::string replacement = env_value;
            replacements.emplace_back(match.position(), match.length(), std::move(replacement));
        }
        // If environment variable not found, leave the pattern unchanged
    }

    // Apply replacements in reverse order
    for (auto it = replacements.rbegin(); it != replacements.rend();
         ++it) { // NOLINT(modernize-loop-convert)
        result.replace(it->position, it->length, it->replacement);
    }

    return result;
}

ConfigManager ConfigManager::copyValuesOnly() const
{
    ConfigManager copy;
    copy.values_ = values_;
    return copy;
}

} // namespace docker_cpp
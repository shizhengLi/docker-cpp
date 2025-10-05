#pragma once

#include <exception>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace docker_cpp {

// Plugin configuration type
using PluginConfig = std::unordered_map<std::string, std::string>;

// Plugin type enumeration
enum class PluginType { CORE, NETWORK, STORAGE, SECURITY, MONITORING, LOGGING, CUSTOM };

// Plugin information structure
// Helper struct to avoid swappable parameters
struct PluginInfoParams {
    std::string name;
    std::string version;
    std::string description;
    PluginType type;
    std::string author = "";
    std::string license = "";
};

class PluginInfo {
public:
    PluginInfo() = default;

    explicit PluginInfo(const PluginInfoParams& params)
        : name_(params.name), version_(params.version), description_(params.description),
          type_(params.type), author_(params.author), license_(params.license)
    {}

    // Getters
    const std::string& getName() const
    {
        return name_;
    }
    const std::string& getVersion() const
    {
        return version_;
    }
    const std::string& getDescription() const
    {
        return description_;
    }
    PluginType getType() const
    {
        return type_;
    }
    const std::string& getAuthor() const
    {
        return author_;
    }
    const std::string& getLicense() const
    {
        return license_;
    }

    // Setters
    void setDescription(const std::string& description)
    {
        description_ = description;
    }
    void setAuthor(const std::string& author)
    {
        author_ = author;
    }
    void setLicense(const std::string& license)
    {
        license_ = license;
    }

    // Convert to string representation
    std::string toString() const
    {
        return name_ + " v" + version_ + " (" + description_ + ") [" + pluginTypeToString(type_)
               + "] by " + author_ + " (" + license_ + ")";
    }

    // Equality operators
    bool operator==(const PluginInfo& other) const
    {
        return name_ == other.name_ && version_ == other.version_;
    }

    bool operator!=(const PluginInfo& other) const
    {
        return !(*this == other);
    }

private:
    std::string name_;
    std::string version_;
    std::string description_;
    PluginType type_;
    std::string author_;
    std::string license_;

    static std::string pluginTypeToString(PluginType type)
    {
        switch (type) {
            case PluginType::CORE:
                return "Core";
            case PluginType::NETWORK:
                return "Network";
            case PluginType::STORAGE:
                return "Storage";
            case PluginType::SECURITY:
                return "Security";
            case PluginType::MONITORING:
                return "Monitoring";
            case PluginType::LOGGING:
                return "Logging";
            case PluginType::CUSTOM:
                return "Custom";
            default:
                return "Unknown";
        }
    }
};

// Base plugin interface
class IPlugin {
public:
    virtual ~IPlugin() = default;

    // Basic plugin information
    virtual std::string getName() const = 0;
    virtual std::string getVersion() const = 0;
    virtual PluginInfo getPluginInfo() const = 0;

    // Plugin lifecycle
    virtual bool initialize(const PluginConfig& config) = 0;
    virtual void shutdown() = 0;
    virtual bool isInitialized() const = 0;

    // Plugin dependencies
    virtual std::vector<std::string> getDependencies() const = 0;
    virtual bool hasDependency(const std::string& plugin_name) const = 0;

    // Plugin capabilities
    virtual std::vector<std::string> getCapabilities() const = 0;
    virtual bool hasCapability(const std::string& capability) const = 0;
};

} // namespace docker_cpp
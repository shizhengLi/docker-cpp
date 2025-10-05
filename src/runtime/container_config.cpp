#include "container_config.hpp"
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <random>
#include <regex>
#include <sstream>

namespace dockercpp {
namespace runtime {

std::string containerStateToString(ContainerState state)
{
    switch (state) {
        case ContainerState::CREATED:
            return "created";
        case ContainerState::STARTING:
            return "starting";
        case ContainerState::RUNNING:
            return "running";
        case ContainerState::PAUSED:
            return "paused";
        case ContainerState::STOPPING:
            return "stopping";
        case ContainerState::STOPPED:
            return "stopped";
        case ContainerState::REMOVING:
            return "removing";
        case ContainerState::REMOVED:
            return "removed";
        case ContainerState::DEAD:
            return "dead";
        case ContainerState::RESTARTING:
            return "restarting";
        case ContainerState::ERROR:
            return "error";
        default:
            return "unknown";
    }
}

ContainerState stringToContainerState(const std::string& state_str)
{
    std::string lower_state = state_str;
    std::transform(lower_state.begin(), lower_state.end(), lower_state.begin(), ::tolower);

    if (lower_state == "created")
        return ContainerState::CREATED;
    if (lower_state == "starting")
        return ContainerState::STARTING;
    if (lower_state == "running")
        return ContainerState::RUNNING;
    if (lower_state == "paused")
        return ContainerState::PAUSED;
    if (lower_state == "stopping")
        return ContainerState::STOPPING;
    if (lower_state == "stopped")
        return ContainerState::STOPPED;
    if (lower_state == "removing")
        return ContainerState::REMOVING;
    if (lower_state == "removed")
        return ContainerState::REMOVED;
    if (lower_state == "dead")
        return ContainerState::DEAD;
    if (lower_state == "restarting")
        return ContainerState::RESTARTING;
    if (lower_state == "error")
        return ContainerState::ERROR;

    return ContainerState::ERROR; // Default to error for unknown states
}

bool ContainerConfig::isValid() const
{
    return validate().empty();
}

std::vector<std::string> ContainerConfig::validate() const
{
    std::vector<std::string> errors;

    // Check required fields
    if (image.empty()) {
        errors.push_back("Container image is required");
    }

    if (name.empty()) {
        errors.push_back("Container name is required");
    }
    else if (!isValidContainerName(name)) {
        errors.push_back("Invalid container name: " + name);
    }

    // Validate working directory if provided
    if (!working_dir.empty() && working_dir[0] != '/') {
        errors.push_back("Working directory must be an absolute path: " + working_dir);
    }

    // Validate environment variables format (KEY=VALUE)
    for (const auto& env_var : env) {
        if (env_var.find('=') == std::string::npos) {
            errors.push_back("Invalid environment variable format: " + env_var
                             + " (should be KEY=VALUE)");
        }
    }

    // Validate CPU settings
    if (resources.cpu_period > 0 && resources.cpu_quota > 0) {
        if (resources.cpu_quota > resources.cpu_period) {
            errors.push_back("CPU quota cannot be greater than CPU period");
        }
    }

    // Validate memory limits
    if (resources.memory_limit > 0 && resources.memory_swap_limit > 0) {
        if (resources.memory_swap_limit < resources.memory_limit) {
            errors.push_back("Memory swap limit cannot be less than memory limit");
        }
    }

    // Validate user format if provided
    if (!security.user.empty()) {
        std::regex user_regex(R"(^(\d+):(\d+)$|^[a-zA-Z_][a-zA-Z0-9_-]*$)");
        if (!std::regex_match(security.user, user_regex)) {
            errors.push_back("Invalid user format: " + security.user
                             + " (should be uid:gid or username)");
        }
    }

    // Validate port mappings
    for (const auto& port_mapping : network.port_mappings) {
        if (port_mapping.container_port == 0) {
            errors.push_back("Container port cannot be 0 in port mapping");
        }
        if (port_mapping.protocol != "tcp" && port_mapping.protocol != "udp") {
            errors.push_back("Invalid protocol in port mapping: " + port_mapping.protocol
                             + " (should be tcp or udp)");
        }
    }

    return errors;
}

std::string ContainerConfig::getEnvironment(const std::string& key) const
{
    std::string prefix = key + "=";
    for (const auto& env_var : env) {
        if (env_var.substr(0, prefix.length()) == prefix) {
            return env_var.substr(prefix.length());
        }
    }
    return "";
}

void ContainerConfig::setEnvironment(const std::string& key, const std::string& value)
{
    // Remove existing environment variable with the same key
    std::string prefix = key + "=";
    env.erase(std::remove_if(env.begin(),
                             env.end(),
                             [&prefix](const std::string& env_var) {
                                 return env_var.substr(0, prefix.length()) == prefix;
                             }),
              env.end());

    // Add new environment variable
    env.push_back(prefix + value);
}

bool ContainerConfig::hasLabel(const std::string& key) const
{
    return labels.find(key) != labels.end();
}

std::string ContainerConfig::getLabel(const std::string& key) const
{
    auto it = labels.find(key);
    return it != labels.end() ? it->second : "";
}

std::chrono::seconds ContainerInfo::getUptime() const
{
    if (state == ContainerState::RUNNING && started_at.time_since_epoch().count() > 0) {
        auto now = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::seconds>(now - started_at);
    }
    else if (started_at.time_since_epoch().count() > 0
             && finished_at.time_since_epoch().count() > 0) {
        return std::chrono::duration_cast<std::chrono::seconds>(finished_at - started_at);
    }
    return std::chrono::seconds(0);
}

std::string generateContainerId()
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);

    std::stringstream ss;
    for (int i = 0; i < 64; ++i) {
        ss << std::hex << dis(gen);
    }

    return ss.str();
}

std::string generateContainerName(const std::string& prefix)
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 35); // 0-9, a-z

    std::stringstream ss;
    ss << prefix;

    // Generate 6 random characters
    for (int i = 0; i < 6; ++i) {
        int num = dis(gen);
        if (num < 10) {
            ss << num;
        }
        else {
            ss << static_cast<char>('a' + (num - 10));
        }
    }

    return ss.str();
}

bool isValidContainerName(const std::string& name)
{
    if (name.empty() || name.length() > 63) {
        return false;
    }

    // Container name validation: alphanumeric, underscores, hyphens, dots
    std::regex name_regex(R"(^[a-zA-Z0-9][a-zA-Z0-9_.-]*$)");
    return std::regex_match(name, name_regex);
}

bool isValidContainerId(const std::string& id)
{
    if (id.empty() || id.length() != 64) {
        return false;
    }

    // Container ID should be 64-character hexadecimal string
    std::regex id_regex(R"(^[a-f0-9]{64}$)");
    return std::regex_match(id, id_regex);
}

} // namespace runtime
} // namespace dockercpp
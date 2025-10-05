#pragma once

#include <exception>
#include <string>
#include <system_error>
#include <unordered_set>

namespace docker_cpp {

/**
 * @brief Error codes for container operations
 */
enum class ErrorCode {
    // Container errors
    CONTAINER_NOT_FOUND = 1000,
    CONTAINER_ALREADY_EXISTS = 1001,
    CONTAINER_START_FAILED = 1002,
    CONTAINER_STOP_FAILED = 1003,
    CONTAINER_REMOVE_FAILED = 1004,
    CONTAINER_STATE_INVALID = 1005,

    // Image errors
    IMAGE_NOT_FOUND = 2000,
    IMAGE_ALREADY_EXISTS = 2001,
    IMAGE_PULL_FAILED = 2002,
    IMAGE_PUSH_FAILED = 2003,
    IMAGE_INVALID_FORMAT = 2004,
    IMAGE_SIZE_EXCEEDED = 2005,

    // Namespace errors
    NAMESPACE_CREATION_FAILED = 3000,
    NAMESPACE_JOIN_FAILED = 3001,
    NAMESPACE_NOT_FOUND = 3002,
    NAMESPACE_PERMISSION_DENIED = 3003,

    // Cgroup errors
    CGROUP_CREATION_FAILED = 4000,
    CGROUP_CONFIG_FAILED = 4001,
    CGROUP_NOT_FOUND = 4002,
    RESOURCE_LIMIT_EXCEEDED = 4003,

    // Network errors
    NETWORK_CREATION_FAILED = 5000,
    NETWORK_CONFIG_FAILED = 5001,
    NETWORK_NOT_FOUND = 5002,
    PORT_BINDING_FAILED = 5003,

    // Storage errors
    STORAGE_MOUNT_FAILED = 6000,
    STORAGE_UNMOUNT_FAILED = 6001,
    STORAGE_INSUFFICIENT_SPACE = 6002,
    VOLUME_NOT_FOUND = 6003,

    // Security errors
    SECURITY_POLICY_VIOLATION = 7000,
    CAPABILITY_DENIED = 7001,
    SECCOMP_FILTER_FAILED = 7002,

    // System errors
    SYSTEM_ERROR = 8000,
    PERMISSION_DENIED = 8001,
    IO_ERROR = 8002,
    NETWORK_ERROR = 8003,

    // Configuration errors
    CONFIG_INVALID = 9000,
    CONFIG_MISSING = 9001,
    INVALID_TYPE = 9002,
    FILE_NOT_FOUND = 9003,

    // Plugin errors
    PLUGIN_NOT_FOUND = 10000,
    PLUGIN_INITIALIZATION_FAILED = 10001,
    PLUGIN_ALREADY_EXISTS = 10002,
    INVALID_PLUGIN = 10003,
    INVALID_PLUGIN_NAME = 10004,
    DUPLICATE_PLUGIN = 10005,
    PLUGIN_LOADER_NOT_SET = 10006,
    PLUGIN_DEPENDENCY_FAILED = 10007,
    CIRCULAR_DEPENDENCY = 10008,
    DIRECTORY_NOT_FOUND = 10009,
    PLUGIN_SHUTDOWN_FAILED = 10010,

    // Generic error
    UNKNOWN_ERROR = 9999
};

/**
 * @brief Custom error category for container errors
 */
class ContainerErrorCategory : public std::error_category {
public:
    const char* name() const noexcept override
    {
        return "docker-cpp";
    }

    std::string message(int ev) const override
    {
        switch (static_cast<ErrorCode>(ev)) {
            case ErrorCode::CONTAINER_NOT_FOUND:
                return "Container not found";
            case ErrorCode::CONTAINER_ALREADY_EXISTS:
                return "Container already exists";
            case ErrorCode::CONTAINER_START_FAILED:
                return "Failed to start container";
            case ErrorCode::CONTAINER_STOP_FAILED:
                return "Failed to stop container";
            case ErrorCode::CONTAINER_REMOVE_FAILED:
                return "Failed to remove container";
            case ErrorCode::CONTAINER_STATE_INVALID:
                return "Container state is invalid";

            case ErrorCode::IMAGE_NOT_FOUND:
                return "Image not found";
            case ErrorCode::IMAGE_ALREADY_EXISTS:
                return "Image already exists";
            case ErrorCode::IMAGE_PULL_FAILED:
                return "Failed to pull image";
            case ErrorCode::IMAGE_PUSH_FAILED:
                return "Failed to push image";
            case ErrorCode::IMAGE_INVALID_FORMAT:
                return "Invalid image format";
            case ErrorCode::IMAGE_SIZE_EXCEEDED:
                return "Image size exceeded limit";

            case ErrorCode::NAMESPACE_CREATION_FAILED:
                return "Failed to create namespace";
            case ErrorCode::NAMESPACE_JOIN_FAILED:
                return "Failed to join namespace";
            case ErrorCode::NAMESPACE_NOT_FOUND:
                return "Namespace not found";
            case ErrorCode::NAMESPACE_PERMISSION_DENIED:
                return "Permission denied for namespace operation";

            case ErrorCode::CGROUP_CREATION_FAILED:
                return "Failed to create cgroup";
            case ErrorCode::CGROUP_CONFIG_FAILED:
                return "Failed to configure cgroup";
            case ErrorCode::CGROUP_NOT_FOUND:
                return "Cgroup not found";
            case ErrorCode::RESOURCE_LIMIT_EXCEEDED:
                return "Resource limit exceeded";

            case ErrorCode::NETWORK_CREATION_FAILED:
                return "Failed to create network";
            case ErrorCode::NETWORK_CONFIG_FAILED:
                return "Failed to configure network";
            case ErrorCode::NETWORK_NOT_FOUND:
                return "Network not found";
            case ErrorCode::PORT_BINDING_FAILED:
                return "Failed to bind port";

            case ErrorCode::STORAGE_MOUNT_FAILED:
                return "Failed to mount storage";
            case ErrorCode::STORAGE_UNMOUNT_FAILED:
                return "Failed to unmount storage";
            case ErrorCode::STORAGE_INSUFFICIENT_SPACE:
                return "Insufficient storage space";
            case ErrorCode::VOLUME_NOT_FOUND:
                return "Volume not found";

            case ErrorCode::SECURITY_POLICY_VIOLATION:
                return "Security policy violation";
            case ErrorCode::CAPABILITY_DENIED:
                return "Capability denied";
            case ErrorCode::SECCOMP_FILTER_FAILED:
                return "Failed to apply seccomp filter";

            case ErrorCode::SYSTEM_ERROR:
                return "System error";
            case ErrorCode::PERMISSION_DENIED:
                return "Permission denied";
            case ErrorCode::IO_ERROR:
                return "I/O error";
            case ErrorCode::NETWORK_ERROR:
                return "Network error";

            case ErrorCode::CONFIG_INVALID:
                return "Invalid configuration";
            case ErrorCode::CONFIG_MISSING:
                return "Missing configuration: Configuration key not found";
            case ErrorCode::INVALID_TYPE:
                return "Invalid type for configuration value";
            case ErrorCode::FILE_NOT_FOUND:
                return "File not found";

            case ErrorCode::PLUGIN_NOT_FOUND:
                return "Plugin not found";
            case ErrorCode::PLUGIN_INITIALIZATION_FAILED:
                return "Plugin initialization failed";
            case ErrorCode::PLUGIN_ALREADY_EXISTS:
                return "Plugin already exists";
            case ErrorCode::INVALID_PLUGIN:
                return "Invalid plugin";
            case ErrorCode::INVALID_PLUGIN_NAME:
                return "Invalid plugin name";
            case ErrorCode::DUPLICATE_PLUGIN:
                return "Duplicate plugin";
            case ErrorCode::PLUGIN_LOADER_NOT_SET:
                return "Plugin loader not set";
            case ErrorCode::PLUGIN_DEPENDENCY_FAILED:
                return "Plugin dependency failed";
            case ErrorCode::CIRCULAR_DEPENDENCY:
                return "Circular dependency detected";
            case ErrorCode::DIRECTORY_NOT_FOUND:
                return "Directory not found";
            case ErrorCode::PLUGIN_SHUTDOWN_FAILED:
                return "Plugin shutdown failed";

            case ErrorCode::UNKNOWN_ERROR:
            default:
                return "Unknown error";
        }
    }
};

/**
 * @brief Get the container error category instance
 */
const ContainerErrorCategory& getContainerErrorCategory();

/**
 * @brief Container exception class
 */
class ContainerError : public std::exception {
public:
    /**
     * @brief Construct a container error
     * @param code The error code
     * @param message The error message
     */
    ContainerError(ErrorCode code, std::string message);

    /**
     * @brief Copy constructor
     */
    ContainerError(const ContainerError& other) noexcept;

    /**
     * @brief Move constructor
     */
    ContainerError(ContainerError&& other) noexcept;

    /**
     * @brief Copy assignment operator
     */
    ContainerError& operator=(const ContainerError& other) noexcept;

    /**
     * @brief Move assignment operator
     */
    ContainerError& operator=(ContainerError&& other) noexcept;

    /**
     * @brief Destructor
     */
    ~ContainerError() noexcept override = default;

    /**
     * @brief Get the error message
     */
    const char* what() const noexcept override;

    /**
     * @brief Get the error code
     */
    ErrorCode getErrorCode() const noexcept;

    /**
     * @brief Get the error code as std::error_code
     */
    std::error_code code() const noexcept;

private:
    ErrorCode error_code_;
    std::string message_;
    mutable std::string full_message_; // Cache for what() result
};

/**
 * @brief Create a container error from a system error
 * @param code The container error code
 * @param sys_error The system error
 * @return Container error with system error information
 */
ContainerError makeSystemError(ErrorCode code, const std::system_error& sys_error);

} // namespace docker_cpp
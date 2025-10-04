#pragma once

#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <docker-cpp/core/error.hpp>
#include <memory>
#include <string>
#include <system_error>

// Linux namespace constants
#ifndef CLONE_NEWNS
    #define CLONE_NEWNS 0x00020000
#endif
#ifndef CLONE_NEWUTS
    #define CLONE_NEWUTS 0x04000000
#endif
#ifndef CLONE_NEWIPC
    #define CLONE_NEWIPC 0x08000000
#endif
#ifndef CLONE_NEWUSER
    #define CLONE_NEWUSER 0x10000000
#endif
#ifndef CLONE_NEWPID
    #define CLONE_NEWPID 0x20000000
#endif
#ifndef CLONE_NEWNET
    #define CLONE_NEWNET 0x40000000
#endif
#ifndef CLONE_NEWCGROUP
    #define CLONE_NEWCGROUP 0x02000000
#endif

namespace docker_cpp {

/**
 * @brief Namespace types supported by the manager
 */
enum class NamespaceType {
    PID = CLONE_NEWPID,
    NETWORK = CLONE_NEWNET,
    MOUNT = CLONE_NEWNS,
    UTS = CLONE_NEWUTS,
    IPC = CLONE_NEWIPC,
    USER = CLONE_NEWUSER,
    CGROUP = CLONE_NEWCGROUP
};

/**
 * @brief RAII wrapper for Linux namespace management
 *
 * This class provides safe creation and management of Linux namespaces
 * using RAII principles. Resources are automatically cleaned up when
 * the object goes out of scope.
 */
class NamespaceManager {
public:
    /**
     * @brief Create a new namespace
     * @param type The type of namespace to create
     * @throws ContainerError if namespace creation fails
     */
    explicit NamespaceManager(NamespaceType type);

    /**
     * @brief Join an existing namespace
     * @param type The type of namespace to join
     * @param fd File descriptor of the namespace to join
     * @throws ContainerError if joining namespace fails
     */
    NamespaceManager(NamespaceType type, int fd);

    /**
     * @brief Join a namespace by process ID
     * @param pid Process ID whose namespace to join
     * @param type The type of namespace to join
     * @throws ContainerError if joining namespace fails
     */
    static void joinNamespace(pid_t pid, NamespaceType type);

    /**
     * @brief Destructor - automatically cleans up namespace
     */
    ~NamespaceManager();

    /**
     * @brief Get the file descriptor for this namespace
     * @return Namespace file descriptor
     */
    int getFd() const
    {
        return fd_;
    }

    /**
     * @brief Get the namespace type
     * @return Namespace type
     */
    NamespaceType getType() const
    {
        return type_;
    }

    /**
     * @brief Check if the namespace is valid
     * @return true if namespace is valid, false otherwise
     */
    bool isValid() const
    {
        return fd_ >= 0;
    }

    // Non-copyable but movable
    NamespaceManager(const NamespaceManager&) = delete;
    NamespaceManager& operator=(const NamespaceManager&) = delete;
    NamespaceManager(NamespaceManager&& other) noexcept;
    NamespaceManager& operator=(NamespaceManager&& other) noexcept;

private:
    NamespaceType type_;
    int fd_;

    void closeNamespace();
    static std::string getNamespacePath(NamespaceType type);
    static int openNamespace(pid_t pid, NamespaceType type);
};

/**
 * @brief Get a human-readable name for a namespace type
 * @param type The namespace type
 * @return String representation of the namespace type
 */
std::string namespaceTypeToString(NamespaceType type);

} // namespace docker_cpp
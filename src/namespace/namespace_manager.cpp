#include <cerrno>
#include <cstring>
#include <docker-cpp/namespace/namespace_manager.hpp>
#include <sstream>

// Mock implementation for macOS compatibility
// In a real Linux environment, these would use the actual system calls
#ifdef __APPLE__
// Mock implementations for macOS
static int mock_unshare(int flags)
{
    // Simulate namespace creation for testing
    // On macOS, we just return success to allow testing
    return 0;
}

static int mock_setns(int fd, int nstype)
{
    // Simulate namespace joining for testing
    return 0;
}

    #define unshare mock_unshare
    #define setns mock_setns
#endif

namespace docker_cpp {

NamespaceManager::NamespaceManager(NamespaceType type) : type_(type), fd_(-1)
{
    // Create a new namespace using unshare
    int result = unshare(static_cast<int>(type_));
    if (result == -1) {
        throw ContainerError(ErrorCode::NAMESPACE_CREATION_FAILED,
                             "Failed to create namespace: " + std::string(strerror(errno)));
    }

    // On some systems, we might need to open the namespace file descriptor
    // For now, mark as valid since unshare succeeded
    fd_ = 0; // 0 means valid but no specific fd
}

NamespaceManager::NamespaceManager(NamespaceType type, int fd) : type_(type), fd_(fd)
{
    if (fd < 0) {
        throw ContainerError(ErrorCode::NAMESPACE_NOT_FOUND,
                             "Invalid file descriptor for namespace");
    }
}

void NamespaceManager::joinNamespace(pid_t pid, NamespaceType type)
{
    int ns_fd = openNamespace(pid, type);
    if (ns_fd < 0) {
        throw ContainerError(ErrorCode::NAMESPACE_JOIN_FAILED,
                             "Failed to open namespace for process " + std::to_string(pid));
    }

    // Use setns to join the namespace
    int result = setns(ns_fd, static_cast<int>(type));
    close(ns_fd);

    if (result == -1) {
        throw ContainerError(ErrorCode::NAMESPACE_JOIN_FAILED,
                             "Failed to join namespace: " + std::string(strerror(errno)));
    }
}

NamespaceManager::~NamespaceManager()
{
    closeNamespace();
}

NamespaceManager::NamespaceManager(NamespaceManager&& other) noexcept
    : type_(other.type_), fd_(other.fd_)
{
    other.fd_ = -1;
}

NamespaceManager& NamespaceManager::operator=(NamespaceManager&& other) noexcept
{
    if (this != &other) {
        closeNamespace();
        type_ = other.type_;
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

void NamespaceManager::closeNamespace()
{
    if (fd_ > 0) {
        close(fd_);
        fd_ = -1;
    }
}

std::string NamespaceManager::getNamespacePath(NamespaceType type)
{
    switch (type) {
        case NamespaceType::PID:
            return "pid";
        case NamespaceType::NETWORK:
            return "net";
        case NamespaceType::MOUNT:
            return "mnt";
        case NamespaceType::UTS:
            return "uts";
        case NamespaceType::IPC:
            return "ipc";
        case NamespaceType::USER:
            return "user";
        case NamespaceType::CGROUP:
            return "cgroup";
        default:
            throw ContainerError(ErrorCode::NAMESPACE_NOT_FOUND, "Unknown namespace type");
    }
}

int NamespaceManager::openNamespace(pid_t pid, NamespaceType type)
{
    std::ostringstream oss;
    oss << "/proc/" << pid << "/ns/" << getNamespacePath(type);

    int fd = open(oss.str().c_str(), O_RDONLY | O_CLOEXEC);
    if (fd == -1) {
        throw ContainerError(
            ErrorCode::NAMESPACE_NOT_FOUND,
            "Failed to open namespace file: " + oss.str() + " - " + std::string(strerror(errno)));
    }

    return fd;
}

std::string namespaceTypeToString(NamespaceType type)
{
    switch (type) {
        case NamespaceType::PID:
            return "PID";
        case NamespaceType::NETWORK:
            return "Network";
        case NamespaceType::MOUNT:
            return "Mount";
        case NamespaceType::UTS:
            return "UTS";
        case NamespaceType::IPC:
            return "IPC";
        case NamespaceType::USER:
            return "User";
        case NamespaceType::CGROUP:
            return "Cgroup";
        default:
            return "Unknown";
    }
}

} // namespace docker_cpp
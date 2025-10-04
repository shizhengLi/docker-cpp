# C++ Systems Programming for Container Runtimes

## Introduction

Modern C++ provides powerful abstractions and features that make it exceptionally well-suited for systems programming, particularly for complex applications like container runtimes. This article explores the C++ patterns, techniques, and best practices that enable robust, efficient, and maintainable systems programming in the context of container implementation.

## Modern C++ Features for Systems Programming

### 1. RAII for Resource Management

Resource Acquisition Is Initialization (RAII) is perhaps C++'s most valuable feature for systems programming. It ensures that resources are properly released even when exceptions occur.

```cpp
#include <fcntl.h>
#include <unistd.h>
#include <system_error>

class FileDescriptor {
public:
    explicit FileDescriptor(const std::string& path, int flags, mode_t mode = 0644)
        : fd_(-1) {
        fd_ = ::open(path.c_str(), flags, mode);
        if (fd_ == -1) {
            throw std::system_error(errno, std::system_category(),
                                  "Failed to open file: " + path);
        }
    }

    ~FileDescriptor() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    // Non-copyable but movable
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;

    FileDescriptor(FileDescriptor&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }

    FileDescriptor& operator=(FileDescriptor&& other) noexcept {
        if (this != &other) {
            if (fd_ >= 0) {
                ::close(fd_);
            }
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    int get() const { return fd_; }
    explicit operator bool() const { return fd_ >= 0; }

    ssize_t read(void* buffer, size_t count) const {
        ssize_t result = ::read(fd_, buffer, count);
        if (result == -1) {
            throw std::system_error(errno, std::system_category(),
                                  "Failed to read from file descriptor");
        }
        return result;
    }

    ssize_t write(const void* buffer, size_t count) const {
        ssize_t result = ::write(fd_, buffer, count);
        if (result == -1) {
            throw std::system_error(errno, std::system_category(),
                                  "Failed to write to file descriptor");
        }
        return result;
    }

private:
    int fd_;
};
```

### 2. Smart Pointers for Automatic Memory Management

```cpp
#include <memory>
#include <vector>
#include <cstring>

// Custom deleter for malloc-allocated memory
struct MallocDeleter {
    void operator()(void* ptr) const {
        std::free(ptr);
    }
};

using UniqueMallocPtr = std::unique_ptr<void, MallocDeleter>;

// Example: Safe buffer allocation for system calls
class SystemCallBuffer {
public:
    explicit SystemCallBuffer(size_t size) : size_(size) {
        buffer_ = UniqueMallocPtr(std::malloc(size_));
        if (!buffer_) {
            throw std::bad_alloc();
        }
    }

    void* get() const { return buffer_.get(); }
    size_t size() const { return size_; }

    void fill(const std::string& data) {
        if (data.size() > size_) {
            throw std::overflow_error("Data too large for buffer");
        }
        std::memcpy(buffer_.get(), data.data(), data.size());
    }

    std::string toString() const {
        return std::string(static_cast<const char*>(buffer_.get()), size_);
    }

private:
    UniqueMallocPtr buffer_;
    size_t size_;
};
```

### 3. Template Metaprogramming for Compile-Time Validation

```cpp
#include <type_traits>

template<typename T>
struct SystemCallTraits;

template<>
struct SystemCallTraits<int> {
    static constexpr bool is_valid_return(int result) {
        return result >= 0;
    }
    static constexpr int error_value = -1;
};

template<>
struct SystemCallTraits<void*> {
    static constexpr bool is_valid_return(void* result) {
        return result != nullptr;
    }
    static constexpr void* error_value = nullptr;
};

template<typename Func, typename... Args>
auto safe_system_call(Func func, Args&&... args) {
    using ReturnType = decltype(func(std::forward<Args>(args)...));

    ReturnType result = func(std::forward<Args>(args)...);

    if (!SystemCallTraits<ReturnType>::is_valid_return(result)) {
        throw std::system_error(errno, std::system_category(),
                              "System call failed");
    }

    return result;
}

// Usage example
void createDirectory(const std::string& path) {
    safe_system_call(::mkdir, path.c_str(), 0755);
}
```

## Error Handling and Exception Safety

### 1. std::expected for Error Propagation (C++23 style)

While C++23 introduces std::expected, we can implement a similar concept for C++20:

```cpp
#include <variant>
#include <string>
#include <system_error>

template<typename T, typename E = std::error_code>
class Expected {
public:
    Expected(T value) : data_(std::move(value)) {}
    Expected(E error) : data_(std::move(error)) {}

    bool has_value() const { return std::holds_alternative<T>(data_); }
    bool has_error() const { return std::holds_alternative<E>(data_); }

    const T& value() const {
        if (has_error()) {
            throw std::bad_variant_access();
        }
        return std::get<T>(data_);
    }

    T& value() {
        if (has_error()) {
            throw std::bad_variant_access();
        }
        return std::get<T>(data_);
    }

    const E& error() const {
        if (has_value()) {
            throw std::bad_variant_access();
        }
        return std::get<E>(data_);
    }

    // Monadic operations
    template<typename F>
    auto map(F&& f) -> Expected<std::invoke_result_t<F, T>, E> {
        if (has_value()) {
            return Expected<std::invoke_result_t<F, T>, E>(f(value()));
        } else {
            return Expected<std::invoke_result_t<F, T>, E>(error());
        }
    }

private:
    std::variant<T, E> data_;
};

// Usage in container operations
Expected<pid_t> createContainerProcess(const ContainerConfig& config) {
    try {
        pid_t pid = safe_system_call(::fork);
        return Expected<pid_t>(pid);
    } catch (const std::system_error& e) {
        return Expected<pid_t>(e.code());
    }
}
```

### 2. Exception-Safe Operations

```cpp
class ExceptionSafeContainer {
public:
    void start(const std::string& command, const std::vector<std::string>& args) {
        // Ensure strong exception safety guarantee

        // 1. Acquire all resources first
        auto resources = acquireResources();

        // 2. If any step fails, all acquired resources are automatically cleaned up
        try {
            setupNamespaces(resources);
            setupCgroups(resources);
            executeCommand(command, args, resources);
        } catch (...) {
            // Resources are automatically cleaned up via RAII
            throw;
        }
    }

private:
    struct ContainerResources {
        std::unique_ptr<PidNamespace> pid_ns;
        std::unique_ptr<NetworkNamespace> net_ns;
        std::unique_ptr<MountNamespace> mnt_ns;
        std::unique_ptr<CgroupManager> cgroup;
        FileDescriptor working_dir;
    };

    ContainerResources acquireResources() {
        ContainerResources resources;

        // Acquire resources with automatic cleanup on failure
        resources.pid_ns = std::make_unique<PidNamespace>();
        resources.net_ns = std::make_unique<NetworkNamespace>();
        resources.mnt_ns = std::make_unique<MountNamespace>();
        resources.cgroup = std::make_unique<CgroupManager>("container_" + generateId());
        resources.working_dir = FileDescriptor(".", O_RDONLY);

        return resources;
    }
};
```

## Concurrency and Thread Safety

### 1. Thread-Safe Container Management

```cpp
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <atomic>

class ContainerRegistry {
public:
    using ContainerId = std::string;
    using ContainerPtr = std::shared_ptr<Container>;

    bool registerContainer(const ContainerId& id, ContainerPtr container) {
        std::unique_lock lock(mutex_);
        auto [it, inserted] = containers_.emplace(id, std::move(container));
        return inserted;
    }

    std::optional<ContainerPtr> getContainer(const ContainerId& id) const {
        std::shared_lock lock(mutex_);
        auto it = containers_.find(id);
        if (it != containers_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    bool removeContainer(const ContainerId& id) {
        std::unique_lock lock(mutex_);
        return containers_.erase(id) > 0;
    }

    std::vector<ContainerId> listContainers() const {
        std::shared_lock lock(mutex_);
        std::vector<ContainerId> ids;
        ids.reserve(containers_.size());

        for (const auto& [id, container] : containers_) {
            ids.push_back(id);
        }

        return ids;
    }

    // Atomic counter for container IDs
    static ContainerId generateContainerId() {
        static std::atomic<uint64_t> counter{0};
        return "container_" + std::to_string(counter.fetch_add(1));
    }

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<ContainerId, ContainerPtr> containers_;
};
```

### 2. Lock-Free Data Structures for High-Performance Paths

```cpp
#include <atomic>

template<typename T>
class LockFreeQueue {
public:
    struct Node {
        std::atomic<T*> data;
        std::atomic<Node*> next;

        Node() : data(nullptr), next(nullptr) {}
        Node(T* value) : data(value), next(nullptr) {}
    };

    LockFreeQueue() : head_(new Node()), tail_(head_.load()) {}

    ~LockFreeQueue() {
        while (Node* head = head_.load()) {
            head_.store(head->next);
            delete head;
        }
    }

    void enqueue(T* value) {
        Node* new_node = new Node(value);
        Node* tail = tail_.load();

        while (true) {
            Node* next = tail->next.load();
            if (tail == tail_.load()) {
                if (next == nullptr) {
                    if (tail->next.compare_exchange_weak(next, new_node)) {
                        tail_.compare_exchange_weak(tail, new_node);
                        break;
                    }
                } else {
                    tail_.compare_exchange_weak(tail, next);
                }
            }
        }
    }

    T* dequeue() {
        while (true) {
            Node* head = head_.load();
            Node* tail = tail_.load();
            Node* next = head->next.load();

            if (head == head_.load()) {
                if (head == tail) {
                    if (next == nullptr) {
                        return nullptr; // Queue is empty
                    }
                    tail_.compare_exchange_weak(tail, next);
                } else {
                    T* value = next->data.load();
                    if (head_.compare_exchange_weak(head, next)) {
                        delete head;
                        return value;
                    }
                }
            }
        }
    }

private:
    std::atomic<Node*> head_;
    std::atomic<Node*> tail_;
};
```

## Performance Optimization Techniques

### 1. Memory Pool Management

```cpp
template<typename T, size_t BlockSize = 1024>
class MemoryPool {
public:
    MemoryPool() : current_block_(nullptr), free_list_(nullptr) {}

    ~MemoryPool() {
        for (auto* block : blocks_) {
            delete[] block;
        }
    }

    T* allocate() {
        if (!free_list_) {
            allocateBlock();
        }

        T* obj = free_list_;
        free_list_ = reinterpret_cast<T*>(*reinterpret_cast<void**>(obj));
        return obj;
    }

    void deallocate(T* obj) {
        *reinterpret_cast<void**>(obj) = free_list_;
        free_list_ = obj;
    }

private:
    struct Block {
        alignas(T) char data[sizeof(T) * BlockSize];
    };

    std::vector<Block*> blocks_;
    Block* current_block_;
    T* free_list_;

    void allocateBlock() {
        current_block_ = new Block();
        blocks_.push_back(current_block_);

        // Build free list
        char* block_data = current_block_->data;
        for (size_t i = 0; i < BlockSize; ++i) {
            T* obj = reinterpret_cast<T*>(block_data + i * sizeof(T));
            *reinterpret_cast<void**>(obj) = free_list_;
            free_list_ = obj;
        }
    }
};

// Custom allocator for STL containers
template<typename T>
class PoolAllocator {
public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using size_type = size_t;
    using difference_type = ptrdiff_t;

    template<typename U>
    struct rebind {
        using other = PoolAllocator<U>;
    };

    PoolAllocator() noexcept = default;

    template<typename U>
    PoolAllocator(const PoolAllocator<U>&) noexcept {}

    pointer allocate(size_type n) {
        if (n != 1) {
            throw std::bad_alloc();
        }
        return pool_.allocate();
    }

    void deallocate(pointer p, size_type n) noexcept {
        if (n == 1) {
            pool_.deallocate(p);
        }
    }

private:
    static MemoryPool<T> pool_;
};

template<typename T>
MemoryPool<T> PoolAllocator<T>::pool_;
```

### 2. Zero-Copy Operations

```cpp
#include <sys/uio.h>
#include <span>

class ZeroCopyBuffer {
public:
    explicit ZeroCopyBuffer(size_t capacity) : capacity_(capacity) {
        data_ = static_cast<char*>(std::malloc(capacity));
        if (!data_) {
            throw std::bad_alloc();
        }
    }

    ~ZeroCopyBuffer() {
        std::free(data_);
    }

    // Prevent copying
    ZeroCopyBuffer(const ZeroCopyBuffer&) = delete;
    ZeroCopyBuffer& operator=(const ZeroCopyBuffer&) = delete;

    // Allow moving
    ZeroCopyBuffer(ZeroCopyBuffer&& other) noexcept
        : data_(other.data_), capacity_(other.capacity_), size_(other.size_) {
        other.data_ = nullptr;
        other.capacity_ = 0;
        other.size_ = 0;
    }

    ssize_t readFromFile(int fd, off_t offset = 0) {
        ssize_t bytes_read = ::pread(fd, data_, capacity_, offset);
        if (bytes_read == -1) {
            throw std::system_error(errno, std::system_category(),
                                  "Failed to read from file");
        }
        size_ = bytes_read;
        return bytes_read;
    }

    ssize_t writeToFile(int fd, off_t offset = 0) const {
        ssize_t bytes_written = ::pwrite(fd, data_, size_, offset);
        if (bytes_written == -1) {
            throw std::system_error(errno, std::system_category(),
                                  "Failed to write to file");
        }
        return bytes_written;
    }

    ssize_t sendToSocket(int sockfd) const {
        ssize_t bytes_sent = ::send(sockfd, data_, size_, MSG_NOSIGNAL);
        if (bytes_sent == -1) {
            throw std::system_error(errno, std::system_category(),
                                  "Failed to send to socket");
        }
        return bytes_sent;
    }

    // Scatter-gather I/O support
    ssize_t readvFromFile(int fd, const std::vector<iovec>& iov) {
        ssize_t bytes_read = ::readv(fd, iov.data(), iov.size());
        if (bytes_read == -1) {
            throw std::system_error(errno, std::system_category(),
                                  "Failed to readv from file");
        }
        return bytes_read;
    }

    std::span<char> data() { return std::span<char>(data_, size_); }
    std::span<const char> data() const { return std::span<const char>(data_, size_); }
    size_t size() const { return size_; }
    size_t capacity() const { return capacity_; }

private:
    char* data_;
    size_t capacity_;
    size_t size_ = 0;
};
```

## Compile-Time Optimization

### 1. Constexpr for Configuration Validation

```cpp
class ContainerConfig {
public:
    static constexpr size_t MAX_MEMORY_LIMIT = 1024 * 1024 * 1024; // 1GB
    static constexpr double MAX_CPU_SHARES = 1024.0;
    static constexpr size_t MIN_MEMORY_LIMIT = 4 * 1024; // 4KB

    constexpr ContainerConfig(size_t memory_limit, double cpu_shares)
        : memory_limit_(memory_limit), cpu_shares_(cpu_shares) {
        if (!isValid()) {
            throw std::invalid_argument("Invalid container configuration");
        }
    }

    constexpr bool isValid() const {
        return memory_limit_ >= MIN_MEMORY_LIMIT &&
               memory_limit_ <= MAX_MEMORY_LIMIT &&
               cpu_shares_ > 0.0 &&
               cpu_shares_ <= MAX_CPU_SHARES;
    }

    constexpr size_t memoryLimit() const { return memory_limit_; }
    constexpr double cpuShares() const { return cpu_shares_; }

private:
    size_t memory_limit_;
    double cpu_shares_;
};

// Compile-time configuration validation
static_assert(ContainerConfig(512 * 1024 * 1024, 512.0).isValid(),
              "Valid configuration should pass validation");
```

### 2. Template Specialization for Common Operations

```cpp
template<typename ResourceType>
class ResourceController;

template<>
class ResourceController<CpuResource> {
public:
    static constexpr const char* controller_name = "cpu";

    void setLimit(pid_t pid, double shares) {
        // Specialized CPU control implementation
        std::string cpu_shares_path = "/sys/fs/cgroup/cpu/" +
                                     std::to_string(pid) + "/cpu.shares";
        writeValueToFile(cpu_shares_path, static_cast<int>(shares * 1024));
    }

private:
    void writeValueToFile(const std::string& path, int value) {
        std::ofstream file(path);
        if (!file) {
            throw std::runtime_error("Failed to write to " + path);
        }
        file << value;
    }
};

template<>
class ResourceController<MemoryResource> {
public:
    static constexpr const char* controller_name = "memory";

    void setLimit(pid_t pid, size_t bytes) {
        // Specialized memory control implementation
        std::string memory_limit_path = "/sys/fs/cgroup/memory/" +
                                       std::to_string(pid) + "/memory.limit_in_bytes";
        writeValueToFile(memory_limit_path, bytes);
    }

private:
    void writeValueToFile(const std::string& path, size_t value) {
        std::ofstream file(path);
        if (!file) {
            throw std::runtime_error("Failed to write to " + path);
        }
        file << value;
    }
};
```

## Debugging and Instrumentation

### 1. Compile-Time Debug Support

```cpp
#ifdef DEBUG
#define DEBUG_LOG(msg) std::cerr << "[DEBUG] " << __FILE__ << ":" << __LINE__ << ": " << msg << std::endl
#else
#define DEBUG_LOG(msg) do {} while(0)
#endif

class DebugContainer {
public:
    void start(const std::string& command) {
        DEBUG_LOG("Starting container with command: " + command);

        try {
            performStart(command);
            DEBUG_LOG("Container started successfully");
        } catch (const std::exception& e) {
            DEBUG_LOG("Container start failed: " + std::string(e.what()));
            throw;
        }
    }

private:
    void performStart(const std::string& command) {
        // Actual implementation
    }
};
```

### 2. Performance Profiling Integration

```cpp
class ProfiledContainer {
public:
    ProfiledContainer() : profiler_("container_operations") {}

    void start(const std::string& command) {
        auto profile_scope = profiler_.startScope("container_start");

        try {
            setupNamespaces();
            setupCgroups();
            executeCommand(command);
        } catch (...) {
            profiler_.recordMetric("container_start_failures", 1);
            throw;
        }

        profiler_.recordMetric("container_start_successes", 1);
    }

private:
    Profiler profiler_;
};

class Profiler {
public:
    explicit Profiler(const std::string& name) : name_(name) {}

    class Scope {
    public:
        Scope(Profiler& profiler, const std::string& operation)
            : profiler_(profiler), operation_(operation) {
            start_time_ = std::chrono::high_resolution_clock::now();
        }

        ~Scope() {
            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                end_time - start_time_);
            profiler_.recordMetric(operation_ + "_duration_us", duration.count());
        }

    private:
        Profiler& profiler_;
        std::string operation_;
        std::chrono::high_resolution_clock::time_point start_time_;
    };

    Scope startScope(const std::string& operation) {
        return Scope(*this, operation);
    }

    void recordMetric(const std::string& name, uint64_t value) {
        metrics_[name] += value;
    }

    void printMetrics() const {
        std::cout << "Profiler metrics for " << name_ << ":\n";
        for (const auto& [name, value] : metrics_) {
            std::cout << "  " << name << ": " << value << "\n";
        }
    }

private:
    std::string name_;
    std::unordered_map<std::string, uint64_t> metrics_;
};
```

## Conclusion

Modern C++ provides an excellent foundation for systems programming, particularly for complex applications like container runtimes. The combination of RAII, smart pointers, templates, and modern concurrency primitives enables the creation of robust, efficient, and maintainable system software.

Key takeaways for container runtime development:

1. **RAII** ensures proper resource cleanup even in error conditions
2. **Smart pointers** provide automatic memory management with zero overhead
3. **Templates** enable compile-time optimization and type safety
4. **Exception safety** guarantees system stability under error conditions
5. **Modern concurrency** primitives enable efficient multi-threaded designs
6. **Performance optimization** techniques ensure competitive runtime performance

These patterns and techniques will be fundamental as we build our docker-cpp implementation, providing both performance and maintainability while ensuring robust error handling and resource management.

## Next Steps

In our next article, "Building a Container Runtime: Core Architecture Patterns," we'll apply these C++ systems programming techniques to design the core architecture of our container runtime, focusing on component design, plugin architecture, and threading patterns.

---

**Previous Article**: [Linux Container Fundamentals](./02-linux-container-fundamentals.md)
**Next Article**: [Building a Container Runtime: Core Architecture Patterns](./04-container-runtime-architecture.md)
**Series Index**: [Table of Contents](./00-table-of-contents.md)
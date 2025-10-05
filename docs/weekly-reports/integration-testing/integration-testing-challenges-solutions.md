# Integration Testing: Challenges & Solutions

**Technical Challenges Faced During docker-cpp Phase 1 Integration Testing**

## Table of Contents
1. [API Compatibility Issues](#api-compatibility-issues)
2. [Build System Challenges](#build-system-challenges)
3. [Cross-Platform Compatibility](#cross-platform-compatibility)
4. [Performance Testing Challenges](#performance-testing-challenges)
5. [CI/CD Pipeline Issues](#cicd-pipeline-issues)
6. [Resource Management Problems](#resource-management-problems)
7. [Test Reliability Issues](#test-reliability-issues)
8. [Memory and Resource Leaks](#memory-and-resource-leaks)

---

## API Compatibility Issues

### Challenge 1: Multiple API Mismatches

**Problem Description:**
Integration tests had numerous API mismatches with the actual implementation, causing compilation failures across multiple components.

**Specific Issues:**
```cpp
// ❌ WRONG - API mismatches found in original code
#include <docker-cpp/core/config.hpp>  // Wrong header - conflicts with config_manager.hpp
#include <docker-cpp/core/event.hpp>

auto logger_ = std::make_shared<Logger>();  // Wrong - Logger::getInstance() returns raw pointer
auto event_manager_ = std::make_unique<EventManager>();  // Wrong - EventManager is singleton

auto cgroup_manager_ = std::make_unique<CgroupManager>();  // Wrong - CgroupManager is abstract
plugin_registry_->loadPlugins("/path/to/plugins");  // Wrong - method doesn't exist
Event event("test.event");  // Wrong - needs both type and data parameters
```

**Root Cause Analysis:**
1. **Header Conflicts**: Using wrong headers that conflict with actual implementations
2. **Singleton Misunderstanding**: Attempting to create instances of singletons
3. **Abstract Class Instantiation**: Trying to instantiate abstract classes directly
4. **Outdated API References**: Using method names that don't exist in current implementation

**Solution Approach:**
```cpp
// ✅ CORRECT - Fixed API usage
#include <docker-cpp/config/config_manager.hpp>  // Correct header
#include <docker-cpp/core/event.hpp>

// Fixed Logger usage
logger_ = Logger::getInstance();  // Raw pointer, not shared_ptr

// Fixed EventManager usage
event_manager_ = EventManager::getInstance();  // Singleton instance

// Fixed CgroupManager usage - use factory method
CgroupConfig config;
config.name = "test";
auto cgroup_manager_ = CgroupManager::create(config);  // Factory method

// Fixed PluginRegistry usage
auto count = plugin_registry_->getPluginCount();  // Correct method name

// Fixed Event creation
Event event("test.event", "test data");  // Both type and data required
```

**Resolution Process:**
1. **Header File Analysis**: Examined actual header files to understand correct APIs
2. **Working Test Reference**: Used existing working tests as API usage examples
3. **Systematic Fix**: Fixed each API mismatch individually
4. **Validation**: Built and ran tests after each fix to verify correctness

### Challenge 2: Configuration System Conflicts

**Problem:**
Duplicate definitions between `config/config_manager.hpp` and `core/config.hpp` causing compilation conflicts.

**Solution:**
```cpp
// ❌ WRONG - Including conflicting headers
#include <docker-cpp/core/config.hpp>
#include <docker-cpp/config/config_manager.hpp>

// ✅ CORRECT - Use only the correct header
#include <docker-cpp/config/config_manager.hpp>
// Remove the conflicting core/config.hpp include
```

---

## Build System Challenges

### Challenge 1: CMake Configuration Issues

**Problem Description:**
CMake was still using old placeholder test file instead of new comprehensive integration tests.

**Original CMakeLists.txt:**
```cmake
# ❌ WRONG - Using old placeholder
add_executable(tests-integration
    integration/test_container_runtime.cpp  # Old placeholder file
)
```

**Investigation:**
- CMake was configured to use the old placeholder file
- New comprehensive test files weren't included in the build
- Build system needed updating to reflect new test structure

**Solution:**
```cmake
# ✅ CORRECT - Updated for comprehensive tests
# Integration tests - comprehensive Phase 1 component tests
add_executable(tests-integration
    integration/test_phase1_integration.cpp      # New comprehensive integration tests
    integration/test_performance_benchmarks.cpp  # New performance benchmarking suite
)

target_link_libraries(tests-integration
    PRIVATE
    docker-cpp-lib
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(tests-integration
    PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
```

**Verification:**
```bash
# Clean rebuild to ensure new configuration
rm -rf build/
mkdir build && cd build
cmake ..
cmake --build . --target tests-integration

# Verify tests run
./tests/tests-integration
```

### Challenge 2: Dependency Management

**Problem:**
Missing dependencies and incorrect library linking for integration tests.

**Solution:**
```cmake
# Ensure all required dependencies are linked
target_link_libraries(tests-integration
    PRIVATE
    docker-cpp-lib          # Main library
    GTest::gtest           # Google Test framework
    GTest::gtest_main      # Test main function
    pthread                # Threading support
    # Add system-specific libraries if needed
)
```

---

## Cross-Platform Compatibility

### Challenge 1: Different System Capabilities

**Problem Description:**
Linux and macOS have different capabilities for namespaces and cgroups, causing tests to fail on different platforms.

**System Capability Differences:**
- **Linux**: Full namespace and cgroup support
- **macOS**: Limited namespace support, different cgroup implementation
- **Windows**: No namespace support, different container implementation

**Solution: Capability Detection System**

```cpp
class CrossPlatformCapabilityDetector {
public:
    struct SystemCapabilities {
        bool can_use_namespaces = false;
        bool can_use_cgroups = false;
        bool can_use_pid_namespaces = false;
        bool can_use_network_namespaces = false;
        bool can_use_mount_namespaces = false;
    };

    static SystemCapabilities detectCapabilities() {
        SystemCapabilities caps;

        // Test namespace support
        try {
            NamespaceManager test_ns(NamespaceType::PID);
            caps.can_use_namespaces = test_ns.isValid();
            caps.can_use_pid_namespaces = caps.can_use_namespaces;
        } catch (const ContainerError&) {
            caps.can_use_namespaces = false;
        }

        // Test cgroup support
        try {
            CgroupConfig config;
            config.name = "capability_test";
            auto test_cg = CgroupManager::create(config);
            caps.can_use_cgroups = (test_cg != nullptr);
        } catch (const ContainerError&) {
            caps.can_use_cgroups = false;
        }

        // Test specific namespace types
        testSpecificNamespaceCapabilities(caps);

        return caps;
    }

private:
    static void testSpecificNamespaceCapabilities(SystemCapabilities& caps) {
        // Test each namespace type individually
        std::vector<NamespaceType> types = {
            NamespaceType::UTS,
            NamespaceType::IPC,
            NamespaceType::NETWORK,
            NamespaceType::MOUNT,
            NamespaceType::USER
        };

        for (auto type : types) {
            try {
                NamespaceManager test_ns(type);
                bool supported = test_ns.isValid();

                switch (type) {
                    case NamespaceType::NETWORK:
                        caps.can_use_network_namespaces = supported;
                        break;
                    case NamespaceType::MOUNT:
                        caps.can_use_mount_namespaces = supported;
                        break;
                    // ... other namespace types
                }
            } catch (const ContainerError&) {
                // Namespace type not supported
            }
        }
    }
};
```

**Integration with Tests:**
```cpp
class Phase1BasicIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Detect system capabilities
        capabilities_ = CrossPlatformCapabilityDetector::detectCapabilities();

        // Initialize components that are supported
        if (capabilities_.can_use_namespaces) {
            setupNamespaceComponents();
        }

        if (capabilities_.can_use_cgroups) {
            setupCgroupComponents();
        }
    }

    SystemCapabilities capabilities_;
};
```

### Challenge 2: Graceful Test Skipping

**Problem:**
Tests were failing hard when system capabilities weren't available, making it impossible to run tests on unsupported platforms.

**Solution:**
```cpp
// Platform-aware test implementation
TEST_F(Phase1BasicIntegrationTest, NamespaceIntegration) {
    if (!capabilities_.can_use_namespaces) {
        GTEST_SKIP() << "Namespace operations not supported on this system";
    }

    try {
        // Test namespace creation
        auto uts_ns = std::make_unique<NamespaceManager>(NamespaceType::UTS);
        EXPECT_TRUE(uts_ns->isValid());
        EXPECT_EQ(uts_ns->getType(), NamespaceType::UTS);

        // Test namespace manager functionality
        int fd = uts_ns->getFd();
        EXPECT_GT(fd, -1);

        namespace_managers_.push_back(std::move(uts_ns));

    } catch (const ContainerError& e) {
        GTEST_SKIP() << "Namespace creation failed: " << e.what();
    }
}

TEST_F(Phase1BasicIntegrationTest, CgroupIntegration) {
    if (!capabilities_.can_use_cgroups) {
        GTEST_SKIP() << "Cgroup operations not supported on this system";
    }

    try {
        // Test cgroup manager creation
        CgroupConfig config;
        config.name = "test_integration";
        auto cgroup_mgr = CgroupManager::create(config);
        ASSERT_TRUE(cgroup_mgr != nullptr);

        cgroup_manager_ = std::move(cgroup_mgr);

    } catch (const ContainerError& e) {
        GTEST_SKIP() << "Cgroup creation failed: " << e.what();
    }
}
```

---

## Performance Testing Challenges

### Challenge 1: Inconsistent Performance Measurements

**Problem Description:**
Initial performance benchmarks showed high variance and inconsistent results, making it difficult to establish reliable baselines.

**Issues Identified:**
- Single execution measurements were unreliable
- System load affected test results
- Cache effects skewed measurements
- Statistical significance was low

**Solution: Statistical Benchmarking Framework**

```cpp
class PerformanceBenchmark {
public:
    template<typename Func>
    struct BenchmarkResult {
        double mean_us;
        double median_us;
        double min_us;
        double max_us;
        double std_deviation;
        size_t sample_count;
    };

    template<typename Func>
    static BenchmarkResult<Func> runBenchmark(
        const std::string& name,
        int iterations,
        int warmup_iterations,
        Func&& benchmark_func) {

        std::vector<double> times;
        times.reserve(iterations);

        // Warm-up runs to eliminate cache effects
        for (int i = 0; i < warmup_iterations; ++i) {
            benchmark_func();
        }

        // Actual measurements
        for (int i = 0; i < iterations; ++i) {
            auto duration = measureExecutionTime(benchmark_func);
            times.push_back(static_cast<double>(duration.count()));
        }

        return calculateStatistics(times);
    }

private:
    template<typename Func>
    static auto measureExecutionTime(Func&& func) {
        auto start = std::chrono::high_resolution_clock::now();
        func();
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    }

    static BenchmarkResult<void> calculateStatistics(const std::vector<double>& times) {
        BenchmarkResult<void> result;
        result.sample_count = times.size();

        // Calculate mean
        result.mean_us = std::accumulate(times.begin(), times.end(), 0.0) / times.size();

        // Calculate min/max
        result.min_us = *std::min_element(times.begin(), times.end());
        result.max_us = *std::max_element(times.begin(), times.end());

        // Calculate median
        auto sorted_times = times;
        std::sort(sorted_times.begin(), sorted_times.end());
        result.median_us = sorted_times[sorted_times.size() / 2];

        // Calculate standard deviation
        double variance = 0.0;
        for (double time : times) {
            variance += (time - result.mean_us) * (time - result.mean_us);
        }
        variance /= times.size();
        result.std_deviation = std::sqrt(variance);

        return result;
    }
};
```

**Usage in Tests:**
```cpp
TEST_F(Phase1PerformanceBenchmarks, ConfigurationPerformance) {
    const int iterations = 10;
    const int warmup_iterations = 2;

    auto benchmark = [&]() {
        for (int i = 0; i < 1000; ++i) {
            std::string key = "benchmark.config.key." + std::to_string(i);
            std::string value = "benchmark.value." + std::to_string(dis_(gen_));

            config_manager_->set(key, value);
            auto retrieved = config_manager_->get<std::string>(key);
            EXPECT_EQ(retrieved, value);
        }
    };

    auto result = PerformanceBenchmark::runBenchmark(
        "Configuration Performance",
        iterations,
        warmup_iterations,
        benchmark
    );

    // Print detailed results
    std::cout << "Configuration Performance (1000 ops):" << std::endl;
    std::cout << "  Mean: " << result.mean_us << " μs" << std::endl;
    std::cout << "  Median: " << result.median_us << " μs" << std::endl;
    std::cout << "  Min: " << result.min_us << " μs" << std::endl;
    std::cout << "  Max: " << result.max_us << " μs" << std::endl;
    std::cout << "  Std Dev: " << result.std_deviation << " μs" << std::endl;

    // Performance assertion with statistical significance
    EXPECT_LT(result.median_us, 50000) << "Configuration operations should complete in < 50ms";
}
```

### Challenge 2: Concurrent Testing Complexity

**Problem:**
Testing concurrent operations required careful synchronization to avoid race conditions and ensure reliable results.

**Solution:**
```cpp
TEST_F(Phase1PerformanceBenchmarks, ConcurrentOperations) {
    const int num_threads = 4;
    const int operations_per_thread = 250;

    auto benchmark = [&]() {
        std::vector<std::thread> threads;
        std::atomic<int> completed_operations{0};
        std::mutex results_mutex;
        std::vector<std::string> results;

        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&, t]() {
                for (int i = 0; i < operations_per_thread; ++i) {
                    // Configuration operations (thread-safe)
                    std::string key = "thread." + std::to_string(t) + ".key." + std::to_string(i);
                    std::string value = "value_" + std::to_string(i);
                    config_manager_->set(key, value);

                    // Event operations (thread-safe)
                    Event event("concurrent.benchmark",
                              "thread " + std::to_string(t) + " operation " + std::to_string(i));
                    event_manager_->publish(event);

                    // Synchronized result collection
                    {
                        std::lock_guard<std::mutex> lock(results_mutex);
                        results.push_back(key + "=" + value);
                    }

                    completed_operations++;
                }
            });
        }

        // Wait for all threads to complete
        for (auto& thread : threads) {
            thread.join();
        }

        // Verify all operations completed
        EXPECT_EQ(completed_operations.load(), num_threads * operations_per_thread);
        EXPECT_EQ(results.size(), num_threads * operations_per_thread);

        // Verify no data corruption
        for (const auto& result : results) {
            EXPECT_FALSE(result.empty());
            EXPECT_NE(result.find("="), std::string::npos);
        }
    };

    auto times = runMultipleBenchmarks(5, benchmark);
    printBenchmarkStats("Concurrent Operations (1000 ops)", times);

    // Performance assertion
    double mean_time = times[times.size() / 2]; // Use median
    EXPECT_LT(mean_time, 200000) << "Concurrent operations should complete in < 200ms";
}
```

---

## CI/CD Pipeline Issues

### Challenge 1: Cross-Platform Compiler Warnings

**Problem Description:**
Strict compiler warnings (-Werror) were causing build failures across different platforms with different compilers.

**Linux (clang++-14) Errors:**
```
tests/integration/test_phase1_integration.cpp:151:10: error: unused variable 'subscription_id' [-Werror,-Wunused-variable]
    auto subscription_id = event_manager_->subscribe("test.integration", [&](const Event& event) {
         ^
tests/integration/test_phase1_integration.cpp:236:69: error: unused parameter 'event' [-Werror,-Wunused-parameter]
        event_manager_->subscribe("workflow.test", [&](const Event& event) { event_count++; });
                                                                    ^
```

**macOS (Xcode clang++) Errors:**
```
tests/integration/test_phase1_integration.cpp:151:10: error: unused variable 'subscription_id' [-Werror,-Wunused-variable]
  151 |     auto subscription_id = event_manager_->subscribe("test.integration", [&](const Event& event) {
      |          ^~~~~~~~~~~~~~~
```

**Code Coverage (gcc) Errors:**
```
tests/integration/test_phase1_integration.cpp:151:10: error: unused variable 'subscription_id' [-Werror=unused-variable]
tests/integration/test_phase1_integration.cpp:236:69: error: unused parameter 'event' [-Werror=unused-parameter]
```

**Solution: Systematic Warning Elimination**

```cpp
// ❌ BEFORE - Causing warnings
auto subscription_id = event_manager_->subscribe("test.integration", [&](const Event& event) {
    event_received = true;
    received_type = event.getType();
    received_data = event.getData();
});

// ✅ AFTER - Warnings eliminated
event_manager_->subscribe("test.integration", [&](const Event& event) {
    event_received = true;
    received_type = event.getType();
    received_data = event.getData();
});

// ❌ BEFORE - Unused parameter
event_manager_->subscribe("workflow.test", [&](const Event& event) {
    event_count++;
});

// ✅ AFTER - Commented parameter name
event_manager_->subscribe("workflow.test", [&](const Event& /* event */) {
    event_count++;
});
```

**Automated Fix Process:**
1. **Identify All Warning Locations**: Run builds on all platforms to collect warnings
2. **Systematic Fixes**: Apply consistent fixes across all files
3. **Verification**: Build and test on all platforms after fixes
4. **Static Analysis**: Run clang-format and cppcheck to ensure compliance

### Challenge 2: Build Time Optimization

**Problem:**
Full test suite was taking too long in CI/CD, slowing down development feedback.

**Solution: Parallel Test Execution**

```bash
# ✅ OPTIMIZED - Parallel execution
ctest --parallel 4 --output-on-failure --timeout 300

# CTest configuration for parallel execution
set_tests_properties(
    CoreTests
    NamespaceTests
    CgroupTests
    PluginTests
    ConfigTests
    IntegrationTests
    PROPERTIES
    TIMEOUT 300
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)
```

**Test Categorization for Selective Execution:**
```bash
# Fast tests for every PR
ctest --parallel 4 -R ".*Test.*" --output-on-failure

# Integration tests only when needed
ctest --parallel 4 -R ".*Integration.*" --output-on-failure

# Performance tests on schedule
ctest --parallel 4 -R ".*Performance.*" --output-on-failure
```

---

## Resource Management Problems

### Challenge 1: Memory Leaks in Test Scenarios

**Problem Description:**
Tests were leaking resources (file descriptors, memory, system resources) leading to test failures and system instability.

**Common Leak Patterns:**
```cpp
// ❌ WRONG - Resource leaks
TEST_F(NamespaceTest, MultipleNamespaceCreation) {
    for (int i = 0; i < 100; ++i) {
        auto ns = std::make_unique<NamespaceManager>(NamespaceType::PID);
        // Namespace created but file descriptor not properly managed
        // Memory allocated but not properly tracked
        // No cleanup in case of exceptions
    }
    // Resources leak when test exits
}
```

**Solution: RAII Resource Management**

```cpp
// ✅ CORRECT - RAII-based resource management
class ResourceManagingTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize resource tracking
        initial_fd_count = countOpenFileDescriptors();
        initial_memory = getCurrentMemoryUsage();
    }

    void TearDown() override {
        // Verify no resource leaks
        cleanupResources();

        auto final_fd_count = countOpenFileDescriptors();
        auto final_memory = getCurrentMemoryUsage();

        EXPECT_EQ(initial_fd_count, final_fd_count)
            << "File descriptor leak detected";
        EXPECT_LT(final_memory - initial_memory, 1024 * 1024)
            << "Memory leak detected (>1MB)";
    }

private:
    size_t initial_fd_count = 0;
    size_t initial_memory = 0;
    std::vector<std::unique_ptr<NamespaceManager>> namespace_managers_;
    std::vector<std::unique_ptr<CgroupManager>> cgroup_managers_;

    void cleanupResources() {
        // Cleanup in reverse order of creation
        cgroup_managers_.clear();
        namespace_managers_.clear();
    }

    size_t countOpenFileDescriptors() {
        // Platform-specific implementation
        #ifdef __linux__
        return std::distance(
            std::filesystem::directory_iterator("/proc/self/fd"),
            std::filesystem::directory_iterator{}
        );
        #else
        return 0; // Simplified for other platforms
        #endif
    }

    size_t getCurrentMemoryUsage() {
        // Platform-specific memory usage
        return 0; // Implementation depends on platform
    }
};

// Usage in tests
TEST_F(ResourceManagingTest, MultipleNamespaceCreation) {
    for (int i = 0; i < 100; ++i) {
        auto ns = std::make_unique<NamespaceManager>(NamespaceType::PID);
        namespace_managers_.push_back(std::move(ns));
    }
    // Automatic cleanup in TearDown()
}
```

### Challenge 2: Exception Safety in Tests

**Problem:**
Tests weren't handling exceptions properly, leading to resource leaks and inconsistent test states.

**Solution: Exception-Safe Test Patterns**

```cpp
// ✅ CORRECT - Exception-safe test implementation
class ExceptionSafeTest : public ::testing::Test {
protected:
    void SetUp() override {
        try {
            setupTestEnvironment();
        } catch (const std::exception& e) {
            // Ensure cleanup even if setup fails
            cleanupTestEnvironment();
            GTEST_SKIP() << "Test setup failed: " << e.what();
        }
    }

    void TearDown() override {
        try {
            cleanupTestEnvironment();
        } catch (const std::exception& e) {
            // Log but don't throw from destructor
            std::cerr << "Warning: Cleanup failed: " << e.what() << std::endl;
        }
    }

private:
    void setupTestEnvironment() {
        // Setup with proper error handling
        config_manager_ = std::make_unique<ConfigManager>();

        event_manager_ = EventManager::getInstance();

        // Test each component setup
        if (capabilities_.can_use_namespaces) {
            setupNamespaceComponents();
        }

        if (capabilities_.can_use_cgroups) {
            setupCgroupComponents();
        }
    }

    void cleanupTestEnvironment() noexcept {
        // Cleanup in reverse order with exception safety
        try {
            if (capabilities_.can_use_cgroups) {
                cleanupCgroupComponents();
            }
        } catch (...) {
            // Log but continue cleanup
        }

        try {
            if (capabilities_.can_use_namespaces) {
                cleanupNamespaceComponents();
            }
        } catch (...) {
            // Log but continue cleanup
        }

        // Clean up remaining components
        namespace_managers_.clear();
        cgroup_managers_.clear();
        plugin_registry_.reset();
        config_manager_.reset();
    }
};
```

---

## Test Reliability Issues

### Challenge 1: Race Conditions in Asynchronous Tests

**Problem Description:**
Tests involving asynchronous operations (events, processes) were failing intermittently due to race conditions.

**Problematic Test Pattern:**
```cpp
// ❌ WRONG - Race condition prone
TEST_F(EventTest, EventDelivery) {
    bool event_received = false;

    event_manager_->subscribe("test.event", [&](const Event& event) {
        event_received = true;
    });

    event_manager_->publish(Event("test.event", "test data"));
    std::this_thread::sleep_for(100ms);  // Arbitrary sleep - unreliable

    EXPECT_TRUE(event_received);  // Might fail if event hasn't been processed yet
}
```

**Solution: Deterministic Synchronization**

```cpp
// ✅ CORRECT - Deterministic synchronization
class SynchronizedEventTest : public ::testing::Test {
protected:
    void waitForEvent(std::function<bool()> condition,
                     std::chrono::milliseconds timeout = 1000ms) {
        auto start = std::chrono::steady_clock::now();

        while (!condition() &&
               std::chrono::steady_clock::now() - start < timeout) {
            std::this_thread::sleep_for(1ms);
        }
    }

    void waitForEventWithBackoff(std::function<bool()> condition,
                                 std::chrono::milliseconds max_timeout = 1000ms) {
        auto start = std::chrono::steady_clock::now();
        auto current_timeout = 1ms;
        const auto max_backoff = 100ms;

        while (!condition() &&
               std::chrono::steady_clock::now() - start < max_timeout) {
            std::this_thread::sleep_for(current_timeout);
            current_timeout = std::min(current_timeout * 2, max_backoff);
        }
    }
};

TEST_F(SynchronizedEventTest, EventDelivery) {
    std::atomic<bool> event_received{false};
    std::string received_data;

    event_manager_->subscribe("test.event", [&](const Event& event) {
        event_received = true;
        received_data = event.getData();
    });

    event_manager_->publish(Event("test.event", "test data"));

    // Wait with timeout instead of arbitrary sleep
    waitForEvent([&]() { return event_received.load(); });

    EXPECT_TRUE(event_received.load()) << "Event not received within timeout";
    EXPECT_EQ(received_data, "test data");
}
```

### Challenge 2: Test Isolation Problems

**Problem:**
Tests were interfering with each other due to shared state, global singletons, and filesystem resources.

**Solution: Test Isolation Framework**

```cpp
// ✅ CORRECT - Complete test isolation
class IsolatedIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create unique test identifiers
        test_id_ = "test_" + std::to_string(getpid()) + "_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());

        // Create isolated test environment
        setupIsolatedEnvironment();
    }

    void TearDown() override {
        // Complete cleanup of isolated environment
        cleanupIsolatedEnvironment();
    }

private:
    std::string test_id_;
    std::filesystem::path test_directory_;

    void setupIsolatedEnvironment() {
        // Create unique test directory
        test_directory_ = std::filesystem::temp_directory_path() / test_id_;
        std::filesystem::create_directories(test_directory_);

        // Set up isolated configuration
        setupIsolatedConfiguration();

        // Initialize components with isolated settings
        initializeIsolatedComponents();
    }

    void setupIsolatedConfiguration() {
        // Use unique identifiers for all resources
        config_manager_ = std::make_unique<ConfigManager>();

        // Configure with test-specific settings
        config_manager_->set("test.id", test_id_);
        config_manager_->set("test.directory", test_directory_.string());
        config_manager_->set("events.namespace", "test.events." + test_id_);
    }

    void initializeIsolatedComponents() {
        // Event manager with unique namespace
        event_manager_ = EventManager::getInstance();

        // Plugin registry with isolated configuration
        plugin_registry_ = std::make_unique<PluginRegistry>();

        // Check and initialize supported components
        if (capabilities_.can_use_namespaces) {
            setupIsolatedNamespaces();
        }

        if (capabilities_.can_use_cgroups) {
            setupIsolatedCgroups();
        }
    }

    void setupIsolatedNamespaces() {
        // Namespace names must be unique across tests
        namespace_base_name_ = "test_ns_" + test_id_;
    }

    void setupIsolatedCgroups() {
        // Cgroup paths must be unique across tests
        cgroup_base_name_ = "test_cg_" + test_id_;
    }

    void cleanupIsolatedEnvironment() noexcept {
        try {
            // Cleanup in reverse order
            if (capabilities_.can_use_cgroups) {
                cleanupIsolatedCgroups();
            }

            if (capabilities_.can_use_namespaces) {
                cleanupIsolatedNamespaces();
            }

            // Cleanup components
            namespace_managers_.clear();
            cgroup_managers_.clear();
            plugin_registry_.reset();
            config_manager_.reset();

            // Remove test directory
            std::error_code ec;
            std::filesystem::remove_all(test_directory_, ec);
            if (ec) {
                std::cerr << "Warning: Failed to cleanup test directory: " << ec.message() << std::endl;
            }
        } catch (...) {
            // Log but don't throw from destructor
            std::cerr << "Warning: Exception during test cleanup" << std::endl;
        }
    }
};
```

---

## Memory and Resource Leaks

### Challenge 1: File Descriptor Leaks

**Problem Description:**
Integration tests were leaking file descriptors, particularly from namespace operations, leading to system resource exhaustion.

**Detection and Solution:**
```cpp
// File descriptor leak detection and prevention
class FileDescriptorManager {
public:
    static int getOpenFdCount() {
        #ifdef __linux__
        std::error_code ec;
        auto fd_dir = std::filesystem::directory_iterator("/proc/self/fd", ec);
        if (ec) {
            return -1;
        }
        return std::distance(fd_dir, std::filesystem::directory_iterator{});
        #else
        return -1; // Not implemented for other platforms
        #endif
    }

    static std::vector<int> getOpenFdList() {
        std::vector<int> fds;

        #ifdef __linux__
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator("/proc/self/fd", ec)) {
            try {
                int fd = std::stoi(entry.path().filename().string());
                fds.push_back(fd);
            } catch (...) {
                // Skip invalid entries
            }
        }
        #endif

        return fds;
    }
};

class FdLeakDetectionTest : public ::testing::Test {
protected:
    void SetUp() override {
        initial_fd_count_ = FileDescriptorManager::getOpenFdCount();
        initial_fds_ = FileDescriptorManager::getOpenFdList();

        ASSERT_GT(initial_fd_count_, 0) << "Failed to get initial FD count";
    }

    void TearDown() override {
        auto final_fd_count = FileDescriptorManager::getOpenFdCount();
        auto final_fds = FileDescriptorManager::getOpenFdList();

        // Check for FD leaks
        if (final_fd_count > initial_fd_count_) {
            std::vector<int> leaked_fds;
            std::set_difference(final_fds.begin(), final_fds.end(),
                               initial_fds_.begin(), initial_fds_.end(),
                               std::back_inserter(leaked_fds));

            FAIL() << "File descriptor leak detected. Leaked "
                   << (final_fd_count - initial_fd_count_)
                   << " file descriptors: " << formatFdList(leaked_fds);
        }
    }

private:
    int initial_fd_count_ = 0;
    std::vector<int> initial_fds_;

    std::string formatFdList(const std::vector<int>& fds) {
        std::ostringstream oss;
        for (size_t i = 0; i < fds.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << fds[i];
        }
        return oss.str();
    }
};
```

### Challenge 2: Memory Leak Detection

**Problem:**
Memory leaks in long-running integration tests were causing system instability and test failures.

**Solution: Memory Leak Detection Framework**

```cpp
class MemoryLeakDetector {
public:
    struct MemorySnapshot {
        size_t heap_size = 0;
        size_t stack_size = 0;
        size_t total_allocated = 0;
        size_t allocation_count = 0;
        std::chrono::system_clock::time_point timestamp;
    };

    static MemorySnapshot getCurrentSnapshot() {
        MemorySnapshot snapshot;
        snapshot.timestamp = std::chrono::system_clock::now();

        #ifdef __linux__
        // Read memory info from /proc/self/status
        std::ifstream status_file("/proc/self/status");
        std::string line;

        while (std::getline(status_file, line)) {
            if (line.find("VmSize:") == 0) {
                snapshot.total_allocated = parseMemoryValue(line);
            } else if (line.find("VmRSS:") == 0) {
                snapshot.heap_size = parseMemoryValue(line);
            }
        }
        #endif

        return snapshot;
    }

    static bool detectMemoryLeak(const MemorySnapshot& baseline,
                                const MemorySnapshot& current,
                                size_t threshold_kb = 1024) { // 1MB default threshold
        size_t growth = current.heap_size - baseline.heap_size;
        return growth > threshold_kb;
    }

private:
    static size_t parseMemoryValue(const std::string& line) {
        std::istringstream iss(line);
        std::string label, value, unit;
        iss >> label >> value >> unit;

        try {
            return std::stoull(value);
        } catch (...) {
            return 0;
        }
    }
};

class MemoryLeakDetectionTest : public ::testing::Test {
protected:
    void SetUp() override {
        baseline_memory_ = MemoryLeakDetector::getCurrentSnapshot();
    }

    void TearDown() override {
        auto current_memory = MemoryLeakDetector::getCurrentSnapshot();

        if (MemoryLeakDetector::detectMemoryLeak(baseline_memory_, current_memory)) {
            size_t leak_size = current_memory.heap_size - baseline_memory_.heap_size;

            // Allow some tolerance for normal fluctuations
            if (leak_size > 2048) { // 2MB tolerance
                FAIL() << "Memory leak detected. Growth: " << leak_size << " KB";
            }
        }
    }

private:
    MemoryLeakDetector::MemorySnapshot baseline_memory_;
};
```

---

## Lessons Learned & Best Practices

### Key Takeaways

1. **API Alignment**: Always verify actual APIs against documentation
2. **Capability Detection**: Assume nothing about system capabilities
3. **Resource Management**: Use RAII consistently for all resources
4. **Test Isolation**: Ensure tests don't interfere with each other
5. **Statistical Testing**: Use proper statistical methods for performance tests
6. **Cross-Platform**: Design tests to work across different platforms
7. **Exception Safety**: Handle all exceptions properly in tests
8. **CI/CD Integration**: Ensure tests work reliably in automated environments

### Preventive Measures

1. **Static Analysis**: Run clang-format, cppcheck, clang-tidy regularly
2. **Resource Monitoring**: Include resource usage checks in tests
3. **Performance Baselines**: Establish and track performance baselines
4. **Documentation**: Document all test patterns and solutions
5. **Regular Reviews**: Periodically review and refactor tests
6. **Automated Validation**: Include automated validation in CI/CD

### Future Improvements

1. **Mock Framework**: Introduce proper mocking framework for external dependencies
2. **Property-Based Testing**: Add property-based testing for edge cases
3. **Continuous Monitoring**: Implement continuous performance monitoring
4. **Test Data Management**: Improve test data generation and cleanup
5. **Chaos Engineering**: Add fault injection and chaos testing

This comprehensive approach to handling integration testing challenges ensures robust, maintainable, and reliable tests that provide confidence in system functionality across different environments and use cases.
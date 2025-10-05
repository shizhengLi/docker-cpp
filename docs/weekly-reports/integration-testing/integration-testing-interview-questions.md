# Integration Testing Interview Questions & Answers

**Based on docker-cpp Phase 1 Integration Testing Experience**

## Table of Contents
1. [Integration Testing Fundamentals](#integration-testing-fundamentals)
2. [System Architecture & Design](#system-architecture--design)
3. [Performance Testing & Benchmarking](#performance-testing--benchmarking)
4. [CI/CD & DevOps](#cicd--devops)
5. [Problem Solving & Troubleshooting](#problem-solving--troubleshooting)
6. [Best Practices & Patterns](#best-practices--patterns)
7. [Advanced Technical Questions](#advanced-technical-questions)
8. [Behavioral & Experience Questions](#behavioral--experience-questions)

---

## Integration Testing Fundamentals

### Q1: What is integration testing and how does it differ from unit testing?

**Answer:** Integration testing verifies that different components or modules of a system work together correctly. Unlike unit testing which tests individual components in isolation, integration testing focuses on the interfaces and interactions between components.

**Key Differences:**
- **Scope**: Unit tests test single functions/classes; integration tests test component interactions
- **Dependencies**: Unit tests use mocks/doubles; integration tests use real components
- **Environment**: Unit tests run in isolation; integration tests require full system setup
- **Focus**: Unit tests verify correctness; integration tests verify compatibility

**Example from our project:**
```cpp
// Unit test - tests single component
TEST(ConfigManagerTest, SetAndGetStringValue) {
    ConfigManager config;
    config.set("test.key", "test_value");
    EXPECT_EQ(config.get<std::string>("test.key"), "test_value");
}

// Integration test - tests component interaction
TEST_F(Phase1BasicIntegrationTest, CompleteWorkflow) {
    // Tests config, events, namespaces, cgroups working together
    config_manager_->set("workflow.test", true);
    event_manager_->subscribe("workflow.test", [&](const Event& event) {
        event_count++;
    });
    // ... workflow execution with multiple components
}
```

### Q2: How do you approach designing an integration testing strategy?

**Answer:** A comprehensive integration testing strategy should include:

**1. Component Interaction Matrix:**
- Identify all component pairs that need to interact
- Map data flow and dependencies
- Prioritize critical integration points

**2. Test Categories:**
- **Component Integration**: Test 2-3 components working together
- **System Integration**: Test complete workflows
- **API Integration**: Test external service integrations
- **Performance Integration**: Test performance under load

**3. Test Environment Setup:**
- Production-like environment
- Realistic data and configurations
- System capability detection

**4. Our Approach:**
```cpp
class Phase1BasicIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize all Phase 1 components
        logger_ = Logger::getInstance();
        config_manager_ = std::make_unique<ConfigManager>();
        event_manager_ = EventManager::getInstance();
        plugin_registry_ = std::make_unique<PluginRegistry>();

        // Check system capabilities
        checkSystemCapabilities();
    }

    void checkSystemCapabilities() {
        try {
            NamespaceManager test_ns(NamespaceType::PID);
            can_use_namespaces_ = test_ns.isValid();
        } catch (const ContainerError&) {
            can_use_namespaces_ = false;
        }
    }
};
```

### Q3: What are the common challenges in integration testing and how do you overcome them?

**Answer:** Common challenges and solutions:

**1. Test Environment Issues:**
- **Challenge**: Different environments have different capabilities
- **Solution**: Implement capability detection and graceful degradation
```cpp
if (!can_use_namespaces_) {
    GTEST_SKIP() << "Namespace operations not supported on this system";
}
```

**2. Test Data Management:**
- **Challenge**: Managing complex test data across components
- **Solution**: Use factory patterns and builders for test data creation

**3. Test Isolation:**
- **Challenge**: Tests interfering with each other
- **Solution**: Proper setup/teardown and resource cleanup
```cpp
void TearDown() override {
    namespace_managers_.clear();
    plugin_registry_.reset();
    config_manager_.reset();
    // Ensure complete cleanup
}
```

**4. Flaky Tests:**
- **Challenge**: Tests that pass/fail inconsistently
- **Solution**: Remove timing dependencies, add proper synchronization

**5. Performance Testing:**
- **Challenge**: Getting consistent performance measurements
- **Solution**: Statistical analysis with multiple runs
```cpp
std::vector<double> runMultipleBenchmarks(int iterations, Func&& benchmark) {
    std::vector<double> times;
    for (int i = 0; i < iterations; ++i) {
        auto duration = measureExecutionTime(benchmark);
        times.push_back(static_cast<double>(duration.count()));
    }
    return times;
}
```

---

## System Architecture & Design

### Q4: How would you design a testable microservices architecture?

**Answer:** Key principles for testable microservices:

**1. Clear Service Boundaries:**
- Well-defined APIs and contracts
- Single responsibility per service
- Loose coupling between services

**2. Integration Points:**
- API contracts with versioning
- Event-driven communication
- Circuit breakers and retries

**3. Testing Strategy:**
```cpp
// Service integration test example
class MicroserviceIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Start services in test mode
        config_service_ = std::make_unique<ConfigService>(test_config);
        event_service_ = std::make_unique<EventService>(test_config);
        namespace_service_ = std::make_unique<NamespaceService>(test_config);

        // Connect services
        wireServices();
    }

    void wireServices() {
        // Configure service communication
        event_service_->subscribe("config.changed",
            [this](const Event& e) { handleConfigChange(e); });

        config_service_->setEventPublisher(event_service_.get());
    }
};
```

### Q5: Explain the importance of dependency injection in testing.

**Answer:** Dependency injection is crucial for testing because it:

**1. Enables Test Isolation:**
- Replace real dependencies with test doubles
- Control dependency behavior in tests
- Test edge cases and error conditions

**2. Facilitates Mocking:**
```cpp
// Without dependency injection (hard to test)
class ContainerManager {
public:
    ContainerManager() {
        logger_ = Logger::getInstance();  // Hard dependency
        config_ = ConfigManager::getInstance();  // Hard dependency
    }
};

// With dependency injection (easy to test)
class ContainerManager {
public:
    ContainerManager(Logger* logger, ConfigManager* config)
        : logger_(logger), config_(config) {}
};

// Test with mocks
TEST(ContainerManagerTest, HandlesConfigChange) {
    auto mock_logger = std::make_unique<MockLogger>();
    auto mock_config = std::make_unique<MockConfig>();

    EXPECT_CALL(*mock_config, get("container.name", _))
        .WillOnce(Return("test-container"));

    ContainerManager manager(mock_logger.get(), mock_config.get());
    // Test behavior
}
```

### Q6: How do you handle external dependencies in integration tests?

**Answer:** Strategies for handling external dependencies:

**1. Test Doubles:**
- **Mocks**: Verify interaction patterns
- **Stubs**: Provide canned responses
- **Fakes**: Lightweight implementations

**2. Service Virtualization:**
```cpp
class MockCgroupService : public ICgroupService {
public:
    MOCK_METHOD(createCgroup, (const CgroupConfig& config), (override));
    MOCK_METHOD(addProcess, (pid_t pid), (override));
    MOCK_METHOD(getMetrics, (), (override, const));
};

// Usage in test
TEST_F(ContainerIntegrationTest, HandlesCgroupOperations) {
    auto mock_cgroup = std::make_unique<MockCgroupService>();

    EXPECT_CALL(*mock_cgroup, createCgroup(_))
        .Times(1)
        .WillOnce(Return(true));

    ContainerManager container_manager(std::move(mock_cgroup));
    // Test container creation
}
```

**3. Contract Testing:**
- Verify API contracts between services
- Use Pact or similar frameworks
- Test provider and consumer contracts

---

## Performance Testing & Benchmarking

### Q7: How do you design effective performance benchmarks?

**Answer:** Effective performance benchmarks require:

**1. Statistical Rigor:**
```cpp
template<typename Func>
std::vector<double> runMultipleBenchmarks(int iterations, Func&& benchmark) {
    std::vector<double> times;
    times.reserve(iterations);

    for (int i = 0; i < iterations; ++i) {
        auto duration = measureExecutionTime(benchmark);
        times.push_back(static_cast<double>(duration.count()));
    }

    return times;
}

void printBenchmarkStats(const std::string& name, std::vector<double> times) {
    double min = *std::min_element(times.begin(), times.end());
    double max = *std::max_element(times.begin(), times.end());
    double mean = std::accumulate(times.begin(), times.end(), 0.0) / times.size();

    std::sort(times.begin(), times.end());
    double median = times[times.size() / 2];

    std::cout << name << " (" << times.size() << " runs):" << std::endl;
    std::cout << "  Mean: " << mean << " μs" << std::endl;
    std::cout << "  Median: " << median << " μs" << std::endl;
    std::cout << "  Min: " << min << " μs" << std::endl;
    std::cout << "  Max: " << max << " μs" << std::endl;
}
```

**2. Realistic Workloads:**
- Test with actual usage patterns
- Include warm-up runs
- Test different load levels

**3. Resource Monitoring:**
```cpp
// Memory usage benchmark
TEST_F(Phase1PerformanceBenchmarks, MemoryUsageBenchmark) {
    auto benchmark = [&]() {
        std::vector<std::unique_ptr<ConfigManager>> configs;

        // Create many configurations
        for (int i = 0; i < 1000; ++i) {
            auto config = std::make_unique<ConfigManager>();
            config->set("memory.test.key", "memory.test.value." + std::to_string(i));
            configs.push_back(std::move(config));
        }

        // Automatic cleanup when vector goes out of scope
    };

    auto times = runMultipleBenchmarks(5, benchmark);
    printBenchmarkStats("Memory Usage (1000 objects)", times);
}
```

### Q8: How do you measure and analyze test performance?

**Answer:** Performance measurement approach:

**1. Precision Timing:**
```cpp
template<typename Func>
auto measureExecutionTime(Func&& func) {
    auto start = std::chrono::high_resolution_clock::now();
    func();
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start);
}
```

**2. Baseline Establishment:**
- Measure current performance
- Set realistic targets
- Track performance over time

**3. Performance Analysis:**
- Identify bottlenecks through profiling
- Analyze memory allocation patterns
- Monitor system resource usage

**4. Our Results:**
```
Configuration System (1000 ops) (10 runs):
  Mean: 314.80 μs
  Median: 307.00 μs
  Min: 306.00 μs
  Max: 384.00 μs

Event System (1000 events) (10 runs):
  Mean: 12958.80 μs
  Median: 13063.00 μs
  Min: 12614.00 μs
  Max: 13163.00 μs
```

### Q9: What strategies do you use for load testing in integration tests?

**Answer:** Load testing strategies:

**1. Concurrent Testing:**
```cpp
TEST_F(Phase1PerformanceBenchmarks, ConcurrentOperations) {
    const int num_threads = 4;
    const int operations_per_thread = 250;

    auto benchmark = [&]() {
        std::vector<std::thread> threads;
        std::atomic<int> completed_operations{0};

        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&, t]() {
                for (int i = 0; i < operations_per_thread; ++i) {
                    // Configuration operations
                    config_manager_->set(
                        "thread." + std::to_string(t) + ".key." + std::to_string(i),
                        "value_" + std::to_string(i));

                    // Event operations
                    Event event("concurrent.benchmark",
                              "thread " + std::to_string(t) + " operation " + std::to_string(i));
                    event_manager_->publish(event);

                    completed_operations++;
                }
            });
        }

        for (auto& thread : threads) {
            thread.join();
        }

        EXPECT_EQ(completed_operations.load(), num_threads * operations_per_thread);
    };

    auto times = runMultipleBenchmarks(5, benchmark);
    printBenchmarkStats("Concurrent Operations (1000 ops)", times);
}
```

**2. Stress Testing:**
- Push system beyond normal limits
- Test degradation behavior
- Verify graceful failure modes

**3. Soak Testing:**
- Long-running tests
- Memory leak detection
- Resource exhaustion handling

---

## CI/CD & DevOps

### Q10: How do you integrate integration tests into CI/CD pipelines?

**Answer:** CI/CD integration strategy:

**1. Pipeline Stages:**
```yaml
# GitHub Actions example
name: Integration Tests

on: [push, pull_request]

jobs:
  test:
    runs-on: ${{ matrix.os }}
    strategy:
      matrix:
        os: [ubuntu-latest, macos-latest]

    steps:
    - uses: actions/checkout@v2

    - name: Setup Build Environment
      run: |
        cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug

    - name: Build
      run: cmake --build build --target all

    - name: Run Integration Tests
      run: |
        cd build
        ctest --parallel 4 --output-on-failure

    - name: Static Analysis
      run: |
        clang-format --dry-run --Werror $(git ls-files '*.cpp' '*.hpp')
        cppcheck --enable=warning,style,performance src/ include/
```

**2. Quality Gates:**
- **Build Success**: All tests must compile and run
- **Test Coverage**: Minimum coverage thresholds
- **Performance**: No performance regression
- **Static Analysis**: Zero violations

**3. Parallel Execution:**
```bash
# Run tests in parallel for faster feedback
ctest --parallel 4 --output-on-failure
```

### Q11: How do you handle flaky tests in CI/CD?

**Answer:** Strategies for handling flaky tests:

**1. Root Cause Analysis:**
- Identify timing dependencies
- Check for race conditions
- Review resource cleanup

**2. Test Isolation:**
```cpp
// Ensure proper test isolation
TEST_F(Phase1BasicIntegrationTest, EventIntegration) {
    bool event_received = false;

    // Use unique identifiers to avoid conflicts
    std::string event_type = "test.integration." + std::to_string(getpid());

    // Clean setup
    event_manager_->subscribe(event_type, [&](const Event& event) {
        event_received = true;
    });

    // Deterministic wait instead of arbitrary sleep
    auto timeout = std::chrono::steady_clock::now() + 100ms;
    while (!event_received && std::chrono::steady_clock::now() < timeout) {
        std::this_thread::sleep_for(1ms);
    }

    EXPECT_TRUE(event_received);
}
```

**3. Retry Mechanisms:**
- Implement smart retries for known flaky tests
- Use exponential backoff
- Track flakiness metrics

### Q12: How do you ensure consistent test environments across CI/CD?

**Answer:** Environment consistency strategies:

**1. Containerization:**
```dockerfile
# Dockerfile for consistent test environment
FROM ubuntu:20.04

RUN apt-get update && apt-get install -y \
    cmake \
    ninja-build \
    clang-14 \
    libgtest-dev \
    # ... other dependencies

WORKDIR /app
COPY . .
RUN cmake -B build -S . && cmake --build build

CMD ["ctest", "--parallel", "4"]
```

**2. Configuration Management:**
```cpp
// System capability detection
bool hasCgroupPermissions() const {
    return std::filesystem::exists("/sys/fs/cgroup") &&
           access("/sys/fs/cgroup", R_OK | W_OK) == 0;
}

// Graceful handling of missing capabilities
TEST_F(Phase1BasicIntegrationTest, CgroupIntegration) {
    if (!can_use_cgroups_) {
        GTEST_SKIP() << "Cgroup operations not supported on this system";
    }
    // Test implementation
}
```

**3. Version Pinning:**
- Pin all tool versions
- Use package managers like Conan
- Document exact environment setup

---

## Problem Solving & Troubleshooting

### Q13: How do you debug failing integration tests?

**Answer:** Systematic debugging approach:

**1. Reproduction Strategy:**
- Isolate the failing test
- Run with verbose logging
- Check system dependencies

**2. Diagnostic Information:**
```cpp
// Enhanced logging for debugging
class DebuggableIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::cout << "Setting up test environment..." << std::endl;

        // Log system capabilities
        std::cout << "Namespace support: " << (can_use_namespaces_ ? "YES" : "NO") << std::endl;
        std::cout << "Cgroup support: " << (can_use_cgroups_ ? "YES" : "NO") << std::endl;

        setupComponents();
    }

    void setupComponents() {
        try {
            config_manager_ = std::make_unique<ConfigManager>();
            std::cout << "Config manager initialized successfully" << std::endl;
        } catch (const std::exception& e) {
            std::cout << "Failed to initialize config manager: " << e.what() << std::endl;
            throw;
        }
    }
};
```

**3. Common Issues and Solutions:**
- **Resource Leaks**: Check for unclosed file descriptors, memory leaks
- **Race Conditions**: Add proper synchronization
- **Environment Issues**: Verify system capabilities and permissions

### Q14: How do you handle performance regression in tests?

**Answer:** Performance regression handling:

**1. Baseline Establishment:**
```cpp
// Performance baseline tracking
struct PerformanceBaseline {
    std::string operation;
    double mean_time_us;
    double max_time_us;
    std::chrono::system_clock::time_point date;
};

class PerformanceRegressionTest : public ::testing::Test {
protected:
    void checkPerformanceRegression(const std::string& operation,
                                   double actual_time) {
        auto baseline = getBaseline(operation);

        EXPECT_LT(actual_time, baseline.max_time_us * 1.2)
            << "Performance regression detected in " << operation
            << ". Expected < " << baseline.max_time_us * 1.2
            << "μs, got " << actual_time << "μs";
    }
};
```

**2. Continuous Monitoring:**
- Track performance metrics over time
- Alert on significant deviations
- Investigate and optimize regressions

**3. Optimization Strategies:**
- Profile slow tests
- Optimize algorithms and data structures
- Reduce I/O operations

### Q15: How do you handle test data management in integration tests?

**Answer:** Test data management strategies:

**1. Factory Pattern for Test Data:**
```cpp
class TestDataFactory {
public:
    static CgroupConfig createTestCgroupConfig(const std::string& name = "test") {
        CgroupConfig config;
        config.name = name;
        config.controllers = CgroupController::CPU | CgroupController::MEMORY;
        config.cpu.max_usec = 500000;
        config.memory.max_bytes = 100 * 1024 * 1024;
        return config;
    }

    static ProcessConfig createTestProcessConfig(const std::string& cmd = "/bin/echo") {
        ProcessConfig config;
        config.executable = cmd;
        config.args = {"test", "output"};
        config.working_directory = "/tmp";
        return config;
    }
};

// Usage in tests
TEST_F(CgroupIntegrationTest, BasicOperations) {
    auto config = TestDataFactory::createTestCgroupConfig("integration_test");
    // Test with the configuration
}
```

**2. Cleanup Strategies:**
```cpp
class ResourceManagingTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create temporary directory for test data
        test_dir_ = std::filesystem::temp_directory_path() / "docker_cpp_test";
        std::filesystem::create_directories(test_dir_);
    }

    void TearDown() override {
        // Ensure complete cleanup
        std::error_code ec;
        std::filesystem::remove_all(test_dir_, ec);
        if (ec) {
            std::cout << "Warning: Failed to cleanup test directory: " << ec.message() << std::endl;
        }
    }

private:
    std::filesystem::path test_dir_;
};
```

---

## Best Practices & Patterns

### Q16: What are the best practices for writing maintainable integration tests?

**Answer:** Best practices for maintainable integration tests:

**1. Clear Test Structure:**
```cpp
TEST_F(Phase1BasicIntegrationTest, EventIntegration) {
    // Arrange
    bool event_received = false;
    std::string received_type;
    std::string received_data;

    // Act
    event_manager_->subscribe("test.integration", [&](const Event& event) {
        event_received = true;
        received_type = event.getType();
        received_data = event.getData();
    });

    Event test_event("test.integration", "integration test data");
    event_manager_->publish(test_event);
    std::this_thread::sleep_for(100ms);

    // Assert
    EXPECT_TRUE(event_received);
    EXPECT_EQ(received_type, "test.integration");
    EXPECT_EQ(received_data, "integration test data");
}
```

**2. Reusable Test Utilities:**
```cpp
class IntegrationTestUtils {
public:
    static void waitForEvent(std::function<bool()> condition,
                           std::chrono::milliseconds timeout = 100ms) {
        auto start = std::chrono::steady_clock::now();
        while (!condition() &&
               std::chrono::steady_clock::now() - start < timeout) {
            std::this_thread::sleep_for(1ms);
        }
    }

    static bool fileExists(const std::string& path) {
        return std::filesystem::exists(path);
    }
};
```

**3. Descriptive Test Names:**
- Use clear, descriptive test names
- Include the scenario and expected outcome
- Follow consistent naming conventions

**4. Test Independence:**
- Tests should not depend on each other
- Clean setup and teardown
- Avoid shared state between tests

### Q17: How do you implement test automation for integration testing?

**Answer:** Test automation implementation:

**1. Test Discovery:**
```cpp
// Use Google Test's automatic test discovery
TEST(Phase1Integration, BasicInitialization) { /* ... */ }
TEST(Phase1Integration, ConfigurationIntegration) { /* ... */ }
TEST(Phase1Integration, EventIntegration) { /* ... */ }
```

**2. Automated Execution:**
```bash
#!/bin/bash
# run_integration_tests.sh

set -e

echo "Building integration tests..."
cmake --build build --target tests-integration

echo "Running integration tests..."
cd build
ctest --parallel 4 --output-on-failure --timeout 300

echo "Generating coverage report..."
gcovr --html --output coverage_html src/ include/

echo "Integration tests completed successfully!"
```

**3. Reporting and Notifications:**
```cpp
// Custom test event listeners for reporting
class CustomTestListener : public ::testing::EmptyTestEventListener {
public:
    void OnTestEnd(const ::testing::TestInfo& test_info) override {
        if (test_info.result()->Passed()) {
            std::cout << "✅ " << test_info.name() << std::endl;
        } else {
            std::cout << "❌ " << test_info.name() << std::endl;
            // Send notification to monitoring system
        }
    }
};
```

### Q18: How do you ensure test reliability and reduce flakiness?

**Answer:** Strategies for reliable tests:

**1. Deterministic Behavior:**
```cpp
// Use deterministic wait conditions
TEST_F(EventIntegrationTest, EventDelivery) {
    std::atomic<bool> event_received{false};

    event_manager_->subscribe("test.event", [&](const Event& event) {
        event_received = true;
    });

    event_manager_->publish(Event("test.event", "test data"));

    // Wait with timeout instead of fixed sleep
    auto start = std::chrono::steady_clock::now();
    while (!event_received &&
           std::chrono::steady_clock::now() - start < 1000ms) {
        std::this_thread::sleep_for(10ms);
    }

    EXPECT_TRUE(event_received) << "Event not received within timeout";
}
```

**2. Resource Management:**
```cpp
// RAII for automatic resource cleanup
class ScopedCgroup {
public:
    explicit ScopedCgroup(const CgroupConfig& config)
        : manager_(CgroupManager::create(config)) {
        if (manager_) {
            manager_->create();
        }
    }

    ~ScopedCgroup() {
        if (manager_) {
            try {
                manager_->destroy();
            } catch (...) {
                // Log error but don't throw in destructor
            }
        }
    }

private:
    std::unique_ptr<CgroupManager> manager_;
};
```

**3. Environment Isolation:**
- Use unique identifiers for resources
- Clean up resources in reverse order of creation
- Handle exceptions gracefully

---

## Advanced Technical Questions

### Q19: How would you implement chaos engineering in integration tests?

**Answer:** Chaos engineering implementation:

**1. Fault Injection:**
```cpp
class ChaosEngine {
public:
    void injectNetworkLatency(std::chrono::milliseconds delay) {
        // Simulate network delays
    }

    void injectProcessFailure(pid_t pid) {
        // Simulate process failure
        kill(pid, SIGKILL);
    }

    void injectResourceExhaustion() {
        // Simulate resource exhaustion
    }
};

TEST_F(ChaosIntegrationTest, SystemResilience) {
    ChaosEngine chaos;

    // Setup system under test
    auto container = createContainer();

    // Inject chaos
    chaos.injectNetworkLatency(500ms);

    // Verify system recovers
    EXPECT_TRUE(container->isHealthy());
}
```

**2. Failure Scenarios:**
- Network partitions
- Process crashes
- Resource exhaustion
- Disk failures

**3. Recovery Verification:**
- Test automatic recovery mechanisms
- Verify health checking
- Ensure graceful degradation

### Q20: How do you implement contract testing between microservices?

**Answer:** Contract testing implementation:

**1. Consumer Contracts:**
```cpp
// Consumer defines expectations
class ConfigServiceConsumerContract {
public:
    void verifyContract() {
        // Mock the provider
        auto mock_provider = std::make_unique<MockConfigProvider>();

        EXPECT_CALL(*mock_provider, getConfig("container.memory"))
            .WillOnce(Return("512m"));

        // Test consumer behavior
        ConfigService consumer(mock_provider.get());
        auto config = consumer.getContainerConfig();

        EXPECT_EQ(config.memory_limit, "512m");
    }
};
```

**2. Provider Verification:**
```cpp
// Provider verifies it meets contracts
class ConfigServiceProviderTest {
public:
    void verifyConsumerContract() {
        ConfigProvider provider;

        // Test actual provider behavior
        auto config = provider.getConfig("container.memory");
        EXPECT_FALSE(config.empty());

        // Verify compatibility with consumer expectations
        EXPECT_TRUE(validateConfigFormat(config));
    }
};
```

**3. Continuous Verification:**
- Run contract tests in CI/CD
- Verify backward compatibility
- Alert on contract violations

### Q21: How would you implement distributed tracing in integration tests?

**Answer:** Distributed tracing implementation:

**1. Tracing Integration:**
```cpp
class TracingIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize tracing
        tracer_ = opentracing::Tracer::Global();

        // Create test span
        auto span = tracer_->StartSpan("integration_test");
        span->SetTag("test.name", ::testing::UnitTest::GetInstance()->current_test_info()->name());
    }

    void traceComponentOperation(const std::string& component,
                                const std::string& operation) {
        auto span = tracer_->StartSpan(component + "." + operation);
        span->Finish();
    }

private:
    std::shared_ptr<opentracing::Tracer> tracer_;
};

TEST_F(TracingIntegrationTest, EndToEndWorkflow) {
    traceComponentOperation("config", "load");
    traceComponentOperation("namespace", "create");
    traceComponentOperation("cgroup", "setup");
    traceComponentOperation("process", "start");

    // Verify trace spans are created
    // Verify span relationships and timing
}
```

**2. Trace Verification:**
- Verify all components are traced
- Check span relationships
- Validate timing data

---

## Behavioral & Experience Questions

### Q22: Tell me about a time when integration tests revealed a critical bug.

**Answer:** "In our docker-cpp project, integration tests revealed a critical memory leak in the namespace management system. The unit tests passed because they only tested individual namespace operations, but the integration test that created multiple namespaces in sequence showed memory usage growing continuously.

**The Issue:**
```cpp
// Problem: File descriptors weren't being closed
class NamespaceManager {
public:
    int getFd() const { return fd_; }
    // Missing proper cleanup in destructor
private:
    int fd_;
};
```

**The Discovery:**
Our integration test created 50 namespaces in a loop and monitored memory usage:
```cpp
TEST_F(NamespaceStressTest, MultipleNamespaceCreation) {
    std::vector<std::unique_ptr<NamespaceManager>> namespaces;

    for (int i = 0; i < 50; ++i) {
        auto ns = std::make_unique<NamespaceManager>(NamespaceType::PID);
        namespaces.push_back(std::move(ns));

        // Memory usage check revealed the leak
        if (i % 10 == 0) {
            auto memory = getMemoryUsage();
            EXPECT_LT(memory, baseline_memory + 10 * 1024 * 1024)
                << "Memory usage exceeded threshold at iteration " << i;
        }
    }
}
```

**The Fix:**
We implemented proper RAII cleanup in the destructor and added resource monitoring to our integration tests."

### Q23: How do you balance thoroughness and execution time in integration testing?

**Answer:** "I use a risk-based approach to balance thoroughness and execution time:

**1. Test Triage:**
- **Critical Path Tests**: Run every time (core functionality)
- **Integration Tests**: Run on every PR (component interactions)
- **Full Suite Tests**: Run nightly (comprehensive coverage)
- **Performance Tests**: Run weekly (performance regression)

**2. Parallel Execution:**
```cpp
// Use CTest parallel execution
ctest --parallel 4 --output-on-failure

// Independent test groups can run in parallel
[Parallel]
[Group 1] Configuration Tests
[Group 2] Event System Tests
[Group 3] Namespace Tests
[Group 4] Cgroup Tests
```

**3. Smart Test Selection:**
- Only run tests affected by code changes
- Use test dependency analysis
- Implement incremental testing

**4. Test Prioritization:**
- High-risk components tested more frequently
- Core functionality gets priority
- Performance tests on schedule, not every build"

### Q24: How do you ensure your integration tests stay relevant as the system evolves?

**Answer:** "I maintain integration test relevance through:

**1. Regular Test Reviews:**
- Quarterly test reviews to assess relevance
- Remove obsolete tests
- Update tests for new features
- Refactor tests for better maintainability

**2. Test Documentation:**
```cpp
// Document test purpose and expected behavior
/**
 * Test verifies complete container lifecycle:
 * 1. Configuration loading and validation
 * 2. Namespace creation and isolation
 * 3. Cgroup setup and resource limits
 * 4. Process creation and monitoring
 * 5. Cleanup and resource recovery
 */
TEST_F(Phase1BasicIntegrationTest, CompleteWorkflow) {
    // Test implementation
}
```

**3. Evolution Strategy:**
- Start with basic integration tests
- Add complexity as system grows
- Refactor tests when architecture changes
- Maintain backward compatibility tests

**4. Metrics and Monitoring:**
- Track test execution time
- Monitor test failure rates
- Measure test coverage
- Identify test maintenance bottlenecks"

### Q25: What's the most challenging integration testing problem you've solved?

**Answer:** "The most challenging problem was implementing cross-platform integration tests for our container runtime system. The challenge was that Linux and macOS have very different capabilities for namespaces and cgroups.

**The Problem:**
```cpp
// Code that works on Linux but fails on macOS
auto ns = std::make_unique<NamespaceManager>(NamespaceType::PID);
ns->unshare();  // Fails on macOS - no namespace support
```

**The Solution:**
I implemented a capability detection system with graceful degradation:

```cpp
class CrossPlatformIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Detect system capabilities
        detectCapabilities();

        // Skip tests if capabilities not available
        if (!hasRequiredCapabilities()) {
            GTEST_SKIP() << "Required capabilities not available on this platform";
        }
    }

    void detectCapabilities() {
        // Test namespace support
        try {
            NamespaceManager test_ns(NamespaceType::PID);
            can_use_namespaces_ = test_ns.isValid();
        } catch (const ContainerError&) {
            can_use_namespaces_ = false;
        }

        // Test cgroup support
        try {
            CgroupConfig config;
            config.name = "capability_test";
            auto test_cg = CgroupManager::create(config);
            can_use_cgroups_ = test_cg != nullptr;
        } catch (const ContainerError&) {
            can_use_cgroups_ = false;
        }
    }

    bool hasRequiredCapabilities() {
        return can_use_namespaces_ && can_use_cgroups_;
    }

private:
    bool can_use_namespaces_ = false;
    bool can_use_cgroups_ = false;
};
```

**The Impact:**
- Tests run successfully on both Linux and macOS
- Clear indication of what's being tested on each platform
- CI/CD pipeline works across platforms
- Developers can run tests regardless of their development environment"

This comprehensive approach ensures our tests are robust, maintainable, and provide valuable feedback throughout the development lifecycle.
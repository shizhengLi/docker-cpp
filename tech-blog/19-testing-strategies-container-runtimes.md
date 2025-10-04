# Docker C++ Implementation - Testing Strategies for Container Runtimes

*Building comprehensive testing frameworks for reliable container runtime systems*

## Table of Contents
1. [Introduction](#introduction)
2. [Testing Architecture Overview](#testing-architecture-overview)
3. [Unit Testing System Interfaces](#unit-testing-system-interfaces)
4. [Integration Testing Framework](#integration-testing-framework)
5. [Chaos Engineering for Containers](#chaos-engineering-for-containers)
6. [Performance Testing](#performance-testing)
7. [Security Testing](#security-testing)
8. [Complete Implementation](#complete-implementation)
9. [Test Automation](#test-automation)
10. [Best Practices](#best-practices)

## Introduction

Testing container runtimes presents unique challenges due to the complexity of interactions between kernels, filesystems, networks, and processes. A comprehensive testing strategy is essential for ensuring reliability, security, and performance in production environments.

This article presents a complete testing framework for our Docker C++ implementation, covering:

- **Unit Testing**: Testing individual components in isolation
- **Integration Testing**: Testing component interactions and workflows
- **Chaos Engineering**: Testing system resilience under failure conditions
- **Performance Testing**: Ensuring system meets performance requirements
- **Security Testing**: Validating security controls and vulnerabilities

## Testing Architecture Overview

### Testing Framework Components

```cpp
// Comprehensive testing architecture
// ┌─────────────────────────────────────────────────────────────────┐
// │                     Test Framework Core                        │
// │ ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ │
// │ │   Unit      │ │ Integration │ │   Chaos     │ │ Performance │ │
// │ │  Tests      │ │   Tests     │ │  Tests      │ │   Tests     │ │
// │ └─────────────┘ └─────────────┘ └─────────────┘ └─────────────┘ │
// └─────────────────────────────────────────────────────────────────┘
//                                │
// ┌─────────────────────────────────────────────────────────────────┐
// │                    Test Infrastructure                          │
// │ ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ │
// │ │ Test        │ │ Mock        │ │ Test        │ │ Coverage    │ │
// │ │ Runners     │ │ Objects     │ │ Fixtures    │ │ Analysis    │ │
// │ └─────────────┘ └─────────────┘ └─────────────┘ └─────────────┘ │
// └─────────────────────────────────────────────────────────────────┘
//                                │
// ┌─────────────────────────────────────────────────────────────────┐
// │                   System Under Test                             │
// │ ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ │
// │ │ Container   │ │ Storage     │ │ Network     │ │ Plugin      │ │
// │ │ Runtime     │ │ Layer       │ │ Layer       │ │ System      │ │
// │ └─────────────┘ └─────────────┘ └─────────────┘ └─────────────┘ │
// └─────────────────────────────────────────────────────────────────┘
```

### Core Testing Infrastructure

```cpp
#pragma once
#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <unordered_map>
#include <chrono>
#include <future>
#include <filesystem>

namespace testing {

// Test result enumeration
enum class TestResult {
    PASSED,
    FAILED,
    SKIPPED,
    TIMEOUT,
    ERROR
};

// Test metadata structure
struct TestMetadata {
    std::string name;
    std::string description;
    std::string category;
    std::string suite;
    std::chrono::milliseconds timeout{5000};
    bool enabled = true;
    std::vector<std::string> dependencies;
    std::unordered_map<std::string, std::string> tags;
};

// Test execution context
class TestContext {
public:
    virtual ~TestContext() = default;

    // Test lifecycle
    virtual void setup() = 0;
    virtual void teardown() = 0;

    // Assertions
    virtual void assertTrue(bool condition, const std::string& message = "") = 0;
    virtual void assertFalse(bool condition, const std::string& message = "") = 0;
    virtual void assertEquals(const std::string& expected, const std::string& actual,
                            const std::string& message = "") = 0;
    virtual void assertEquals(size_t expected, size_t actual,
                            const std::string& message = "") = 0;
    virtual void assertThrows(const std::function<void()>& func,
                            const std::string& expectedException,
                            const std::string& message = "") = 0;

    // Logging and reporting
    virtual void logInfo(const std::string& message) = 0;
    virtual void logWarning(const std::string& message) = 0;
    virtual void logError(const std::string& message) = 0;

    // Resource management
    virtual std::string createTempDirectory() = 0;
    virtual std::string createTempFile() = 0;
    virtual void cleanupTempResources() = 0;

    // Test configuration
    virtual std::string getConfig(const std::string& key) const = 0;
    virtual void setConfig(const std::string& key, const std::string& value) = 0;
};

// Test interface
class ITest {
public:
    virtual ~ITest() = default;

    virtual TestMetadata getMetadata() const = 0;
    virtual TestResult execute(TestContext* context) = 0;
    virtual void beforeTest(TestContext* context) {}
    virtual void afterTest(TestContext* context) {}
};

// Test suite interface
class ITestSuite {
public:
    virtual ~ITestSuite() = default;

    virtual std::string getName() const = 0;
    virtual void addTest(std::unique_ptr<ITest> test) = 0;
    virtual void addSuite(std::unique_ptr<ITestSuite> suite) = 0;
    virtual const std::vector<std::unique_ptr<ITest>>& getTests() const = 0;
    virtual const std::vector<std::unique_ptr<ITestSuite>>& getSuites() const = 0;
    virtual void beforeAll(TestContext* context) {}
    virtual void afterAll(TestContext* context) {}
};

// Test runner interface
class ITestRunner {
public:
    virtual ~ITestRunner() = default;

    virtual bool run(ITest* test, TestContext* context) = 0;
    virtual bool run(ITestSuite* suite, TestContext* context) = 0;
    virtual void setReporter(std::shared_ptr<class ITestReporter> reporter) = 0;
    virtual void setTimeout(std::chrono::milliseconds timeout) = 0;
};

// Test reporter interface
class ITestReporter {
public:
    virtual ~ITestReporter() = default;

    virtual void reportTestStart(const TestMetadata& metadata) = 0;
    virtual void reportTestEnd(const TestMetadata& metadata, TestResult result,
                             const std::string& message, std::chrono::milliseconds duration) = 0;
    virtual void reportSuiteStart(const std::string& suiteName) = 0;
    virtual void reportSuiteEnd(const std::string& suiteName) = 0;
    virtual void reportSummary(size_t total, size_t passed, size_t failed, size_t skipped) = 0;
    virtual void reportError(const std::string& error) = 0;
};

// Mock object interface
class IMockObject {
public:
    virtual ~IMockObject() = default;

    virtual void expectCall(const std::string& method,
                           const std::vector<std::any>& args) = 0;
    virtual void returnValue(const std::any& value) = 0;
    virtual void throwException(const std::exception& ex) = 0;
    virtual void verifyCalls() = 0;
    virtual void reset() = 0;
};

// Test fixture interface
class ITestFixture {
public:
    virtual ~ITestFixture() = default;

    virtual void setup() = 0;
    virtual void teardown() = 0;
    virtual std::string getName() const = 0;
    virtual std::shared_ptr<IMockObject> createMock(const std::string& name) = 0;
};

} // namespace testing
```

## Unit Testing System Interfaces

### Comprehensive Unit Testing Framework

```cpp
#include <iostream>
#include <sstream>
#include <iomanip>
#include <regex>

namespace testing {

// Base test class with common functionality
class TestBase : public ITest {
protected:
    TestMetadata metadata_;
    std::vector<std::function<void()>> cleanup_actions_;

public:
    TestBase(const std::string& name, const std::string& category = "unit") {
        metadata_.name = name;
        metadata_.category = category;
        metadata_.suite = "default";
    }

    TestMetadata getMetadata() const override {
        return metadata_;
    }

    TestResult execute(TestContext* context) override {
        try {
            beforeTest(context);
            runTest(context);
            afterTest(context);
            return TestResult::PASSED;
        } catch (const std::exception& e) {
            context->logError("Test failed with exception: " + std::string(e.what()));
            return TestResult::FAILED;
        } catch (...) {
            context->logError("Test failed with unknown exception");
            return TestResult::ERROR;
        }
    }

    virtual void runTest(TestContext* context) = 0;

    // Helper methods
    void addCleanupAction(std::function<void()> action) {
        cleanup_actions_.push_back(action);
    }

    void runCleanupActions() {
        for (auto it = cleanup_actions_.rbegin(); it != cleanup_actions_.rend(); ++it) {
            try {
                (*it)();
            } catch (const std::exception& e) {
                std::cerr << "Cleanup action failed: " << e.what() << std::endl;
            }
        }
        cleanup_actions_.clear();
    }
};

// Test context implementation
class ConcreteTestContext : public TestContext {
private:
    std::vector<std::string> temp_directories_;
    std::vector<std::string> temp_files_;
    std::unordered_map<std::string, std::string> config_;
    std::ostringstream log_stream_;
    int assertion_count_ = 0;
    int failure_count_ = 0;

public:
    void setup() override {
        // Initialize test environment
        std::filesystem::create_directories("/tmp/docker_test");
    }

    void teardown() override {
        // Clean up temporary resources
        for (const auto& dir : temp_directories_) {
            try {
                std::filesystem::remove_all(dir);
            } catch (...) {}
        }
        for (const auto& file : temp_files_) {
            try {
                std::filesystem::remove(file);
            } catch (...) {}
        }
        temp_directories_.clear();
        temp_files_.clear();
    }

    void assertTrue(bool condition, const std::string& message = "") override {
        assertion_count_++;
        if (!condition) {
            failure_count_++;
            std::string full_message = message.empty() ? "Assertion failed" : message;
            logError("assertTrue failed: " + full_message);
            throw std::runtime_error(full_message);
        }
    }

    void assertFalse(bool condition, const std::string& message = "") override {
        assertion_count_++;
        if (condition) {
            failure_count_++;
            std::string full_message = message.empty() ? "Assertion failed" : message;
            logError("assertFalse failed: " + full_message);
            throw std::runtime_error(full_message);
        }
    }

    void assertEquals(const std::string& expected, const std::string& actual,
                     const std::string& message = "") override {
        assertion_count_++;
        if (expected != actual) {
            failure_count_++;
            std::string full_message = message.empty() ?
                "Expected: '" + expected + "', Actual: '" + actual + "'" : message;
            logError("assertEquals failed: " + full_message);
            throw std::runtime_error(full_message);
        }
    }

    void assertEquals(size_t expected, size_t actual,
                     const std::string& message = "") override {
        assertion_count_++;
        if (expected != actual) {
            failure_count_++;
            std::string full_message = message.empty() ?
                "Expected: " + std::to_string(expected) +
                ", Actual: " + std::to_string(actual) : message;
            logError("assertEquals failed: " + full_message);
            throw std::runtime_error(full_message);
        }
    }

    void assertThrows(const std::function<void()>& func,
                     const std::string& expectedException,
                     const std::string& message = "") override {
        assertion_count_++;
        bool exception_thrown = false;
        std::string actual_exception;

        try {
            func();
        } catch (const std::exception& e) {
            exception_thrown = true;
            actual_exception = typeid(e).name();
        } catch (...) {
            exception_thrown = true;
            actual_exception = "unknown";
        }

        if (!exception_thrown) {
            failure_count_++;
            std::string full_message = message.empty() ?
                "Expected exception but none was thrown" : message;
            logError("assertThrows failed: " + full_message);
            throw std::runtime_error(full_message);
        }
    }

    void logInfo(const std::string& message) override {
        log_stream_ << "[INFO] " << message << std::endl;
    }

    void logWarning(const std::string& message) override {
        log_stream_ << "[WARN] " << message << std::endl;
    }

    void logError(const std::string& message) override {
        log_stream_ << "[ERROR] " << message << std::endl;
    }

    std::string createTempDirectory() override {
        std::string temp_dir = "/tmp/docker_test/dir_" + std::to_string(getpid()) + "_" +
                              std::to_string(temp_directories_.size());
        std::filesystem::create_directories(temp_dir);
        temp_directories_.push_back(temp_dir);
        return temp_dir;
    }

    std::string createTempFile() override {
        std::string temp_file = "/tmp/docker_test/file_" + std::to_string(getpid()) + "_" +
                               std::to_string(temp_files_.size());
        std::ofstream file(temp_file);
        file.close();
        temp_files_.push_back(temp_file);
        return temp_file;
    }

    void cleanupTempResources() override {
        teardown();
    }

    std::string getConfig(const std::string& key) const override {
        auto it = config_.find(key);
        return it != config_.end() ? it->second : "";
    }

    void setConfig(const std::string& key, const std::string& value) override {
        config_[key] = value;
    }

    std::string getLogOutput() const {
        return log_stream_.str();
    }

    int getAssertionCount() const { return assertion_count_; }
    int getFailureCount() const { return failure_count_; }
};

// Mock object implementation
class MockObject : public IMockObject {
private:
    struct ExpectedCall {
        std::string method;
        std::vector<std::any> args;
        std::any return_value;
        std::exception_ptr exception_to_throw;
        bool called = false;
    };

    std::string name_;
    std::vector<ExpectedCall> expected_calls_;
    std::mutex calls_mutex_;

public:
    MockObject(const std::string& name) : name_(name) {}

    void expectCall(const std::string& method,
                   const std::vector<std::any>& args) override {
        std::lock_guard<std::mutex> lock(calls_mutex_);
        expected_calls_.push_back({method, args, {}, nullptr, false});
    }

    void returnValue(const std::any& value) override {
        std::lock_guard<std::mutex> lock(calls_mutex_);
        if (!expected_calls_.empty()) {
            expected_calls_.back().return_value = value;
        }
    }

    void throwException(const std::exception& ex) override {
        std::lock_guard<std::mutex> lock(calls_mutex_);
        if (!expected_calls_.empty()) {
            expected_calls_.back().exception_to_throw = std::make_exception_ptr(ex);
        }
    }

    void verifyCalls() override {
        std::lock_guard<std::mutex> lock(calls_mutex_);
        for (const auto& call : expected_calls_) {
            if (!call.called) {
                throw std::runtime_error("Expected call to " + call.method + " was not made");
            }
        }
    }

    void reset() override {
        std::lock_guard<std::mutex> lock(calls_mutex_);
        expected_calls_.clear();
    }

    // Helper method to simulate method calls
    template<typename... Args>
    std::any callMethod(const std::string& method, Args&&... args) {
        std::vector<std::any> actual_args = {args...};

        std::lock_guard<std::mutex> lock(calls_mutex_);

        for (auto& expected_call : expected_calls_) {
            if (expected_call.method == method && !expected_call.called) {
                if (argsMatch(expected_call.args, actual_args)) {
                    expected_call.called = true;

                    if (expected_call.exception_to_throw) {
                        std::rethrow_exception(expected_call.exception_to_throw);
                    }

                    return expected_call.return_value;
                }
            }
        }

        throw std::runtime_error("Unexpected call to " + method);
    }

private:
    bool argsMatch(const std::vector<std::any>& expected, const std::vector<std::any>& actual) {
        if (expected.size() != actual.size()) return false;

        for (size_t i = 0; i < expected.size(); ++i) {
            // Simple equality check - could be enhanced for type-specific comparisons
            if (expected[i].type() != actual[i].type()) return false;
        }

        return true;
    }
};

// Test fixture implementation
class TestFixture : public ITestFixture {
private:
    std::string name_;
    std::unordered_map<std::string, std::shared_ptr<IMockObject>> mocks_;

public:
    TestFixture(const std::string& name) : name_(name) {}

    void setup() override {
        // Setup common test environment
    }

    void teardown() override {
        // Cleanup common test environment
        mocks_.clear();
    }

    std::string getName() const override {
        return name_;
    }

    std::shared_ptr<IMockObject> createMock(const std::string& name) override {
        auto mock = std::make_shared<MockObject>(name);
        mocks_[name] = mock;
        return mock;
    }
};

// Container runtime unit tests
class ContainerRuntimeTests {
public:
    // Test container creation
    class ContainerCreationTest : public TestBase {
    public:
        ContainerCreationTest() : TestBase("ContainerCreation", "runtime") {}

        void runTest(TestContext* context) override {
            context->logInfo("Testing container creation with valid configuration");

            // Test valid container creation
            auto runtime = createMockRuntime();
            auto config = createValidConfig();

            auto container = runtime->createContainer(config);
            context->assertTrue(container != nullptr, "Container should be created successfully");

            // Verify container properties
            context->assertEquals("test-container", container->getId());
            context->assertEquals("running", container->getStatus());

            context->logInfo("Container creation test completed successfully");
        }

    private:
        std::shared_ptr<IMockObject> createMockRuntime() {
            auto mock = std::make_shared<MockObject>("runtime");
            mock->expectCall("createContainer", {});
            mock->returnValue(std::make_shared<MockContainer>());
            return mock;
        }

        std::unordered_map<std::string, std::string> createValidConfig() {
            return {
                {"image", "ubuntu:20.04"},
                {"name", "test-container"},
                {"command", "/bin/bash"}
            };
        }

        class MockContainer {
        public:
            std::string getId() const { return "test-container"; }
            std::string getStatus() const { return "running"; }
        };
    };

    // Test container lifecycle
    class ContainerLifecycleTest : public TestBase {
    public:
        ContainerLifecycleTest() : TestBase("ContainerLifecycle", "runtime") {}

        void runTest(TestContext* context) override {
            context->logInfo("Testing container lifecycle management");

            // Create container
            auto container = createContainer(context);
            context->assertEquals("created", container->getStatus());

            // Start container
            container->start();
            context->assertEquals("running", container->getStatus());

            // Pause container
            container->pause();
            context->assertEquals("paused", container->getStatus());

            // Resume container
            container->resume();
            context->assertEquals("running", container->getStatus());

            // Stop container
            container->stop();
            context->assertEquals("stopped", container->getStatus());

            // Remove container
            container->remove();
            context->assertTrue(container->isRemoved());

            context->logInfo("Container lifecycle test completed successfully");
        }

    private:
        std::shared_ptr<MockContainer> createContainer(TestContext* context) {
            auto container = std::make_shared<MockContainer>();
            addCleanupAction([container]() { container->cleanup(); });
            return container;
        }

        class MockContainer {
        private:
            std::string status_ = "created";
            bool removed_ = false;

        public:
            void start() { status_ = "running"; }
            void pause() { status_ = "paused"; }
            void resume() { status_ = "running"; }
            void stop() { status_ = "stopped"; }
            void remove() { removed_ = true; }
            std::string getStatus() const { return status_; }
            bool isRemoved() const { return removed_; }
            void cleanup() { /* Cleanup resources */ }
        };
    };

    // Test container networking
    class ContainerNetworkingTest : public TestBase {
    public:
        ContainerNetworkingTest() : TestBase("ContainerNetworking", "network") {}

        void runTest(TestContext* context) override {
            context->logInfo("Testing container networking functionality");

            // Create container with network configuration
            auto container = createContainerWithNetwork(context);
            auto networks = container->listNetworks();

            context->assertEquals(1, networks.size(), "Container should have one network");
            context->assertEquals("bridge", networks[0]);

            // Test IP address assignment
            auto ipAddress = container->getIpAddress();
            context->assertTrue(isValidIpAddress(ipAddress), "IP address should be valid");

            // Test port forwarding
            bool portForwarded = container->exposePort(8080, 80);
            context->assertTrue(portForwarded, "Port forwarding should be successful");

            // Test DNS resolution
            auto resolvedIp = container->resolveDns("localhost");
            context->assertEquals("127.0.0.1", resolvedIp);

            context->logInfo("Container networking test completed successfully");
        }

    private:
        std::shared_ptr<MockContainer> createContainerWithNetwork(TestContext* context) {
            auto container = std::make_shared<MockContainer>();
            container->addNetwork("bridge", "172.17.0.2");
            addCleanupAction([container]() { container->removeNetwork("bridge"); });
            return container;
        }

        bool isValidIpAddress(const std::string& ip) {
            std::regex ipPattern(R"(^(\d{1,3}\.){3}\d{1,3}$)");
            return std::regex_match(ip, ipPattern);
        }

        class MockContainer {
        private:
            std::unordered_map<std::string, std::string> networks_;

        public:
            void addNetwork(const std::string& name, const std::string& ip) {
                networks_[name] = ip;
            }

            std::vector<std::string> listNetworks() const {
                std::vector<std::string> result;
                for (const auto& [name, ip] : networks_) {
                    result.push_back(name);
                }
                return result;
            }

            std::string getIpAddress() const {
                return networks_.empty() ? "" : networks_.begin()->second;
            }

            bool exposePort(uint16_t hostPort, uint16_t containerPort) {
                return true; // Simulate successful port forwarding
            }

            std::string resolveDns(const std::string& hostname) {
                if (hostname == "localhost") return "127.0.0.1";
                return "";
            }

            void removeNetwork(const std::string& name) {
                networks_.erase(name);
            }
        };
    };

    // Test storage operations
    class ContainerStorageTest : public TestBase {
    public:
        ContainerStorageTest() : TestBase("ContainerStorage", "storage") {}

        void runTest(TestContext* context) override {
            context->logInfo("Testing container storage operations");

            std::string tempDir = context->createTempDirectory();
            addCleanupAction([tempDir]() { std::filesystem::remove_all(tempDir); });

            // Create volume
            auto volume = createVolume(context, "test-volume", tempDir);
            context->assertTrue(volume != nullptr, "Volume should be created");

            // Mount volume
            std::string mountPoint = tempDir + "/mount";
            std::filesystem::create_directories(mountPoint);
            context->assertTrue(volume->mount(mountPoint), "Volume should be mounted");

            // Write test file
            std::string testFile = mountPoint + "/test.txt";
            std::ofstream file(testFile);
            file << "Hello, World!";
            file.close();

            // Verify file exists
            context->assertTrue(std::filesystem::exists(testFile), "Test file should exist");

            // Unmount volume
            context->assertTrue(volume->unmount(), "Volume should be unmounted");

            // Remove volume
            context->assertTrue(volume->remove(), "Volume should be removed");

            context->logInfo("Container storage test completed successfully");
        }

    private:
        std::shared_ptr<MockVolume> createVolume(TestContext* context,
                                               const std::string& name,
                                               const std::string& path) {
            auto volume = std::make_shared<MockVolume>(name, path);
            addCleanupAction([volume]() { volume->cleanup(); });
            return volume;
        }

        class MockVolume {
        private:
            std::string name_;
            std::string path_;
            bool mounted_ = false;

        public:
            MockVolume(const std::string& name, const std::string& path)
                : name_(name), path_(path) {}

            bool mount(const std::string& mountPoint) {
                mounted_ = true;
                return true;
            }

            bool unmount() {
                mounted_ = false;
                return true;
            }

            bool remove() {
                return std::filesystem::remove_all(path_) > 0;
            }

            void cleanup() {
                if (mounted_) {
                    unmount();
                }
            }
        };
    };

    // Test security policies
    class ContainerSecurityTest : public TestBase {
    public:
        ContainerSecurityTest() : TestBase("ContainerSecurity", "security") {}

        void runTest(TestContext* context) override {
            context->logInfo("Testing container security policies");

            // Create container with security profile
            auto container = createSecureContainer(context);
            auto policies = container->getSecurityPolicies();

            context->assertTrue(policies.count("no_new_privileges"), "no_new_privileges policy should be present");
            context->assertTrue(policies["no_new_privileges"], "no_new_privileges should be enabled");

            // Test capability dropping
            auto capabilities = container->getCapabilities();
            context->assertTrue(capabilities.empty(), "All capabilities should be dropped by default");

            // Test seccomp filtering
            bool seccompEnabled = container->isSeccompEnabled();
            context->assertTrue(seccompEnabled, "Seccomp should be enabled");

            // Test user namespace isolation
            bool userNamespace = container->hasUserNamespace();
            context->assertTrue(userNamespace, "User namespace should be enabled");

            context->logInfo("Container security test completed successfully");
        }

    private:
        std::shared_ptr<MockSecureContainer> createSecureContainer(TestContext* context) {
            auto container = std::make_shared<MockSecureContainer>();
            addCleanupAction([container]() { container->destroy(); });
            return container;
        }

        class MockSecureContainer {
        private:
            std::unordered_map<std::string, bool> security_policies_ = {
                {"no_new_privileges", true},
                {"seccomp", true},
                {"user_namespace", true}
            };

        public:
            std::unordered_map<std::string, bool> getSecurityPolicies() const {
                return security_policies_;
            }

            std::vector<std::string> getCapabilities() const {
                return {}; // No capabilities
            }

            bool isSeccompEnabled() const {
                return security_policies_.count("seccomp") && security_policies_.at("seccomp");
            }

            bool hasUserNamespace() const {
                return security_policies_.count("user_namespace") && security_policies_.at("user_namespace");
            }

            void destroy() {
                security_policies_.clear();
            }
        };
    };

    // Register all tests
    static std::vector<std::unique_ptr<ITest>> createTests() {
        std::vector<std::unique_ptr<ITest>> tests;

        tests.push_back(std::make_unique<ContainerCreationTest>());
        tests.push_back(std::make_unique<ContainerLifecycleTest>());
        tests.push_back(std::make_unique<ContainerNetworkingTest>());
        tests.push_back(std::make_unique<ContainerStorageTest>());
        tests.push_back(std::make_unique<ContainerSecurityTest>());

        return tests;
    }
};

} // namespace testing
```

## Integration Testing Framework

### Comprehensive Integration Testing

```cpp
#include <thread>
#include <atomic>
#include <future>

namespace testing {

// Integration test base class
class IntegrationTestBase : public TestBase {
protected:
    std::unique_ptr<TestContext> context_;
    std::unique_ptr<ITestFixture> fixture_;

public:
    IntegrationTestBase(const std::string& name) : TestBase(name, "integration") {
        metadata_.timeout = std::chrono::milliseconds(30000); // Longer timeout for integration tests
    }

    void beforeTest(TestContext* context) override {
        TestBase::beforeTest(context);
        context_ = std::make_unique<ConcreteTestContext>();
        fixture_ = std::make_unique<TestFixture>(metadata_.name);
        fixture_->setup();
    }

    void afterTest(TestContext* context) override {
        if (fixture_) {
            fixture_->teardown();
        }
        if (context_) {
            context_->cleanupTempResources();
        }
        TestBase::afterTest(context);
    }
};

// Container runtime integration tests
class ContainerIntegrationTests {
public:
    // End-to-end container workflow test
    class EndToEndWorkflowTest : public IntegrationTestBase {
    public:
        EndToEndWorkflowTest() : IntegrationTestBase("EndToEndWorkflow") {}

        void runTest(TestContext* context) override {
            context_->logInfo("Starting end-to-end container workflow test");

            // Initialize container runtime
            auto runtime = createContainerRuntime();
            context_->assertTrue(runtime != nullptr, "Container runtime should be initialized");

            // Pull image
            context_->logInfo("Pulling Ubuntu image");
            bool pulled = runtime->pullImage("ubuntu:20.04");
            context_->assertTrue(pulled, "Image pull should succeed");

            // Create container
            context_->logInfo("Creating container");
            std::unordered_map<std::string, std::string> config = {
                {"image", "ubuntu:20.04"},
                {"name", "integration-test-container"},
                {"command", "sleep 3600"}
            };

            auto containerId = runtime->createContainer(config);
            context_->assertFalse(containerId.empty(), "Container should be created with valid ID");

            // Start container
            context_->logInfo("Starting container");
            bool started = runtime->startContainer(containerId);
            context_->assertTrue(started, "Container should start successfully");

            // Verify container is running
            auto status = runtime->getContainerStatus(containerId);
            context_->assertEquals("running", status, "Container should be in running state");

            // Create volume and mount
            context_->logInfo("Creating and mounting volume");
            std::string volumeName = "test-volume-" + containerId;
            bool volumeCreated = runtime->createVolume(volumeName, "/data");
            context_->assertTrue(volumeCreated, "Volume should be created");

            bool volumeMounted = runtime->mountVolume(volumeName, containerId, "/mnt/data");
            context_->assertTrue(volumeMounted, "Volume should be mounted");

            // Execute command in container
            context_->logInfo("Executing command in container");
            std::string output;
            bool executed = runtime->executeCommand(containerId, "echo 'Hello from container' > /mnt/data/test.txt", output);
            context_->assertTrue(executed, "Command should execute successfully");

            // Verify file was created
            bool fileExists = runtime->fileExistsInContainer(containerId, "/mnt/data/test.txt");
            context_->assertTrue(fileExists, "Test file should exist in container");

            // Expose port
            context_->logInfo("Exposing container port");
            bool portExposed = runtime->exposePort(containerId, 8080, 80);
            context_->assertTrue(portExposed, "Port should be exposed successfully");

            // Connect to network
            context_->logInfo("Connecting container to network");
            bool networkConnected = runtime->connectToNetwork(containerId, "bridge");
            context_->assertTrue(networkConnected, "Container should connect to network");

            // Stop and remove container
            context_->logInfo("Cleaning up container");
            bool stopped = runtime->stopContainer(containerId);
            context_->assertTrue(stopped, "Container should stop successfully");

            bool removed = runtime->removeContainer(containerId);
            context_->assertTrue(removed, "Container should be removed successfully");

            // Clean up volume
            bool volumeRemoved = runtime->removeVolume(volumeName);
            context_->assertTrue(volumeRemoved, "Volume should be removed successfully");

            context_->logInfo("End-to-end workflow test completed successfully");
        }

    private:
        std::shared_ptr<MockContainerRuntime> createContainerRuntime() {
            return std::make_shared<MockContainerRuntime>();
        }

        class MockContainerRuntime {
        private:
            std::unordered_map<std::string, std::string> containers_;
            std::unordered_map<std::string, std::string> volumes_;

        public:
            bool pullImage(const std::string& image) {
                return true; // Simulate successful image pull
            }

            std::string createContainer(const std::unordered_map<std::string, std::string>& config) {
                std::string containerId = "container-" + std::to_string(containers_.size());
                containers_[containerId] = "created";
                return containerId;
            }

            bool startContainer(const std::string& containerId) {
                auto it = containers_.find(containerId);
                if (it != containers_.end()) {
                    it->second = "running";
                    return true;
                }
                return false;
            }

            std::string getContainerStatus(const std::string& containerId) {
                auto it = containers_.find(containerId);
                return it != containers_.end() ? it->second : "";
            }

            bool createVolume(const std::string& volumeName, const std::string& path) {
                volumes_[volumeName] = path;
                return true;
            }

            bool mountVolume(const std::string& volumeName, const std::string& containerId, const std::string& mountPoint) {
                return volumes_.count(volumeName) > 0 && containers_.count(containerId) > 0;
            }

            bool executeCommand(const std::string& containerId, const std::string& command, std::string& output) {
                output = "Command executed successfully";
                return containers_.count(containerId) > 0;
            }

            bool fileExistsInContainer(const std::string& containerId, const std::string& path) {
                return containers_.count(containerId) > 0;
            }

            bool exposePort(const std::string& containerId, uint16_t hostPort, uint16_t containerPort) {
                return containers_.count(containerId) > 0;
            }

            bool connectToNetwork(const std::string& containerId, const std::string& network) {
                return containers_.count(containerId) > 0;
            }

            bool stopContainer(const std::string& containerId) {
                auto it = containers_.find(containerId);
                if (it != containers_.end()) {
                    it->second = "stopped";
                    return true;
                }
                return false;
            }

            bool removeContainer(const std::string& containerId) {
                return containers_.erase(containerId) > 0;
            }

            bool removeVolume(const std::string& volumeName) {
                return volumes_.erase(volumeName) > 0;
            }
        };
    };

    // Multi-container networking test
    class MultiContainerNetworkingTest : public IntegrationTestBase {
    public:
        MultiContainerNetworkingTest() : IntegrationTestBase("MultiContainerNetworking") {}

        void runTest(TestContext* context) override {
            context_->logInfo("Starting multi-container networking test");

            auto runtime = createMultiContainerRuntime();

            // Create network
            std::string networkId = runtime->createNetwork("test-network", "172.20.0.0/16");
            context_->assertFalse(networkId.empty(), "Network should be created");

            // Create two containers
            std::string container1Id = runtime->createContainer("ubuntu:20.04", "web-server");
            std::string container2Id = runtime->createContainer("ubuntu:20.04", "database");

            context_->assertFalse(container1Id.empty(), "Container 1 should be created");
            context_->assertFalse(container2Id.empty(), "Container 2 should be created");

            // Start containers
            context_->assertTrue(runtime->startContainer(container1Id), "Container 1 should start");
            context_->assertTrue(runtime->startContainer(container2Id), "Container 2 should start");

            // Connect containers to network
            context_->assertTrue(runtime->connectContainerToNetwork(container1Id, networkId), "Container 1 should connect to network");
            context_->assertTrue(runtime->connectContainerToNetwork(container2Id, networkId), "Container 2 should connect to network");

            // Test connectivity between containers
            context_->logInfo("Testing inter-container connectivity");
            bool connected = runtime->testConnectivity(container1Id, container2Id);
            context_->assertTrue(connected, "Containers should be able to communicate");

            // Test DNS resolution
            context_->logInfo("Testing DNS resolution");
            std::string ip = runtime->resolveName(container1Id, "database");
            context_->assertFalse(ip.empty(), "Database should be resolvable from web server");

            // Test port exposure
            context_->logInfo("Testing port exposure");
            context_->assertTrue(runtime->exposePort(container1Id, 8080, 80), "Port 80 should be exposed");
            context_->assertTrue(runtime->exposePort(container2Id, 5432, 5432), "Port 5432 should be exposed");

            // Test external access
            context_->logInfo("Testing external access");
            bool accessible = runtime->testExternalAccess("localhost", 8080);
            context_->assertTrue(accessible, "External access should work");

            // Cleanup
            context_->assertTrue(runtime->disconnectContainerFromNetwork(container1Id, networkId), "Container 1 should disconnect");
            context_->assertTrue(runtime->disconnectContainerFromNetwork(container2Id, networkId), "Container 2 should disconnect");
            context_->assertTrue(runtime->removeContainer(container1Id), "Container 1 should be removed");
            context_->assertTrue(runtime->removeContainer(container2Id), "Container 2 should be removed");
            context_->assertTrue(runtime->removeNetwork(networkId), "Network should be removed");

            context_->logInfo("Multi-container networking test completed successfully");
        }

    private:
        std::shared_ptr<MockMultiContainerRuntime> createMultiContainerRuntime() {
            return std::make_shared<MockMultiContainerRuntime>();
        }

        class MockMultiContainerRuntime {
        private:
            std::unordered_map<std::string, std::string> containers_;
            std::unordered_map<std::string, std::string> networks_;
            std::unordered_map<std::string, std::vector<std::string>> network_connections_;

        public:
            std::string createNetwork(const std::string& name, const std::string& subnet) {
                std::string networkId = "network-" + std::to_string(networks_.size());
                networks_[networkId] = subnet;
                network_connections_[networkId] = {};
                return networkId;
            }

            std::string createContainer(const std::string& image, const std::string& name) {
                std::string containerId = "container-" + std::to_string(containers_.size());
                containers_[containerId] = "running";
                return containerId;
            }

            bool startContainer(const std::string& containerId) {
                auto it = containers_.find(containerId);
                if (it != containers_.end()) {
                    it->second = "running";
                    return true;
                }
                return false;
            }

            bool connectContainerToNetwork(const std::string& containerId, const std::string& networkId) {
                if (containers_.count(containerId) && networks_.count(networkId)) {
                    network_connections_[networkId].push_back(containerId);
                    return true;
                }
                return false;
            }

            bool testConnectivity(const std::string& container1Id, const std::string& container2Id) {
                // Simulate connectivity test
                return containers_.count(container1Id) && containers_.count(container2Id);
            }

            std::string resolveName(const std::string& containerId, const std::string& name) {
                return "172.20.0.2"; // Simulate DNS resolution
            }

            bool exposePort(const std::string& containerId, uint16_t hostPort, uint16_t containerPort) {
                return containers_.count(containerId) > 0;
            }

            bool testExternalAccess(const std::string& host, uint16_t port) {
                return true; // Simulate successful external access
            }

            bool disconnectContainerFromNetwork(const std::string& containerId, const std::string& networkId) {
                auto& connections = network_connections_[networkId];
                auto it = std::find(connections.begin(), connections.end(), containerId);
                if (it != connections.end()) {
                    connections.erase(it);
                    return true;
                }
                return false;
            }

            bool removeContainer(const std::string& containerId) {
                return containers_.erase(containerId) > 0;
            }

            bool removeNetwork(const std::string& networkId) {
                networks_.erase(networkId);
                network_connections_.erase(networkId);
                return true;
            }
        };
    };

    // Volume and storage integration test
    class VolumeStorageIntegrationTest : public IntegrationTestBase {
    public:
        VolumeStorageIntegrationTest() : IntegrationTestBase("VolumeStorageIntegration") {}

        void runTest(TestContext* context) override {
            context_->logInfo("Starting volume and storage integration test");

            auto storageRuntime = createStorageRuntime();
            std::string testDir = context_->createTempDirectory();

            // Create multiple volumes
            std::vector<std::string> volumes;
            for (int i = 0; i < 3; ++i) {
                std::string volumeName = "volume-" + std::to_string(i);
                std::string volumePath = testDir + "/" + volumeName;

                bool created = storageRuntime->createVolume(volumeName, volumePath);
                context_->assertTrue(created, "Volume " + volumeName + " should be created");

                volumes.push_back(volumeName);
            }

            // Create container and mount volumes
            std::string containerId = storageRuntime->createContainer("ubuntu:20.04", "storage-test");
            context_->assertFalse(containerId.empty(), "Container should be created");

            for (size_t i = 0; i < volumes.size(); ++i) {
                std::string mountPoint = "/mnt/volume" + std::to_string(i);
                bool mounted = storageRuntime->mountVolume(volumes[i], containerId, mountPoint);
                context_->assertTrue(mounted, "Volume " + volumes[i] + " should be mounted");
            }

            // Start container
            context_->assertTrue(storageRuntime->startContainer(containerId), "Container should start");

            // Write test data to each volume
            for (size_t i = 0; i < volumes.size(); ++i) {
                std::string mountPoint = "/mnt/volume" + std::to_string(i);
                std::string testFile = mountPoint + "/test" + std::to_string(i) + ".txt";

                std::string command = "echo 'Volume " + std::to_string(i) + " data' > " + testFile;
                std::string output;
                bool executed = storageRuntime->executeCommand(containerId, command, output);
                context_->assertTrue(executed, "Test data should be written to volume " + volumes[i]);
            }

            // Verify data persistence
            context_->assertTrue(storageRuntime->stopContainer(containerId), "Container should be stopped");
            context_->assertTrue(storageRuntime->startContainer(containerId), "Container should be restarted");

            for (size_t i = 0; i < volumes.size(); ++i) {
                std::string mountPoint = "/mnt/volume" + std::to_string(i);
                std::string testFile = mountPoint + "/test" + std::to_string(i) + ".txt";

                bool fileExists = storageRuntime->fileExistsInContainer(containerId, testFile);
                context_->assertTrue(fileExists, "Test file should exist after restart in volume " + volumes[i]);

                std::string output;
                bool read = storageRuntime->executeCommand(containerId, "cat " + testFile, output);
                context_->assertTrue(read, "Test file should be readable");

                std::string expected = "Volume " + std::to_string(i) + " data";
                context_->assertEquals(expected, output, "File content should match expected value");
            }

            // Test volume backup and restore
            context_->logInfo("Testing volume backup and restore");
            std::string backupPath = testDir + "/backup.tar.gz";
            bool backedUp = storageRuntime->backupVolume(volumes[0], backupPath);
            context_->assertTrue(backedUp, "Volume should be backed up successfully");

            bool restored = storageRuntime->restoreVolume(volumes[0], backupPath);
            context_->assertTrue(restored, "Volume should be restored successfully");

            // Cleanup
            context_->assertTrue(storageRuntime->stopContainer(containerId), "Container should be stopped");
            for (const auto& volume : volumes) {
                context_->assertTrue(storageRuntime->removeVolume(volume), "Volume should be removed");
            }
            context_->assertTrue(storageRuntime->removeContainer(containerId), "Container should be removed");

            context_->logInfo("Volume and storage integration test completed successfully");
        }

    private:
        std::shared_ptr<MockStorageRuntime> createStorageRuntime() {
            return std::make_shared<MockStorageRuntime>();
        }

        class MockStorageRuntime {
        private:
            std::unordered_map<std::string, std::string> containers_;
            std::unordered_map<std::string, std::string> volumes_;
            std::unordered_map<std::string, std::vector<std::string>> volume_mounts_;

        public:
            bool createVolume(const std::string& volumeName, const std::string& path) {
                volumes_[volumeName] = path;
                volume_mounts_[volumeName] = {};
                return true;
            }

            std::string createContainer(const std::string& image, const std::string& name) {
                std::string containerId = "container-" + std::to_string(containers_.size());
                containers_[containerId] = "created";
                return containerId;
            }

            bool mountVolume(const std::string& volumeName, const std::string& containerId, const std::string& mountPoint) {
                if (volumes_.count(volumeName) && containers_.count(containerId)) {
                    volume_mounts_[volumeName].push_back(containerId);
                    return true;
                }
                return false;
            }

            bool startContainer(const std::string& containerId) {
                auto it = containers_.find(containerId);
                if (it != containers_.end()) {
                    it->second = "running";
                    return true;
                }
                return false;
            }

            bool stopContainer(const std::string& containerId) {
                auto it = containers_.find(containerId);
                if (it != containers_.end()) {
                    it->second = "stopped";
                    return true;
                }
                return false;
            }

            bool executeCommand(const std::string& containerId, const std::string& command, std::string& output) {
                if (containers_.count(containerId) > 0) {
                    output = "Command executed successfully";
                    return true;
                }
                return false;
            }

            bool fileExistsInContainer(const std::string& containerId, const std::string& path) {
                return containers_.count(containerId) > 0;
            }

            bool backupVolume(const std::string& volumeName, const std::string& backupPath) {
                return volumes_.count(volumeName) > 0;
            }

            bool restoreVolume(const std::string& volumeName, const std::string& backupPath) {
                return volumes_.count(volumeName) > 0;
            }

            bool removeVolume(const std::string& volumeName) {
                volume_mounts_.erase(volumeName);
                return volumes_.erase(volumeName) > 0;
            }

            bool removeContainer(const std::string& containerId) {
                return containers_.erase(containerId) > 0;
            }
        };
    };

    // Security integration test
    class SecurityIntegrationTest : public IntegrationTestBase {
    public:
        SecurityIntegrationTest() : IntegrationTestBase("SecurityIntegration") {}

        void runTest(TestContext* context) override {
            context_->logInfo("Starting security integration test");

            auto securityRuntime = createSecurityRuntime();

            // Create container with security profile
            std::unordered_map<std::string, std::string> securityConfig = {
                {"user", "1000"},
                {"read_only", "true"},
                {"no_new_privileges", "true"},
                {"cap_drop", "ALL"},
                {"security_opt", "no-new-privileges:true"},
                {"tmpfs", "/tmp:rw,noexec,nosuid,size=65536k"}
            };

            std::string containerId = securityRuntime->createContainer("nginx:alpine", securityConfig);
            context_->assertFalse(containerId.empty(), "Security container should be created");

            // Start container
            context_->assertTrue(securityRuntime->startContainer(containerId), "Security container should start");

            // Test privilege escalation prevention
            context_->logInfo("Testing privilege escalation prevention");
            bool escalationPrevented = securityRuntime->testPrivilegeEscalation(containerId);
            context_->assertTrue(escalationPrevented, "Privilege escalation should be prevented");

            // Test read-only filesystem
            context_->logInfo("Testing read-only filesystem");
            bool readOnlyEnforced = securityRuntime->testReadOnlyFilesystem(containerId);
            context_->assertTrue(readOnlyEnforced, "Read-only filesystem should be enforced");

            // Test capability dropping
            context_->logInfo("Testing capability dropping");
            auto capabilities = securityRuntime->getCapabilities(containerId);
            context_->assertEquals(0, capabilities.size(), "All capabilities should be dropped");

            // Test seccomp filtering
            context_->logInfo("Testing seccomp filtering");
            bool seccompActive = securityRuntime->isSeccompActive(containerId);
            context_->assertTrue(seccompActive, "Seccomp filtering should be active");

            // Test resource limits
            context_->logInfo("Testing resource limits");
            auto memoryLimit = securityRuntime->getMemoryLimit(containerId);
            context_->assertTrue(memoryLimit <= 512 * 1024 * 1024, "Memory limit should be enforced");

            // Test process isolation
            context_->logInfo("Testing process isolation");
            bool isolated = securityRuntime->testProcessIsolation(containerId);
            context_->assertTrue(isolated, "Process isolation should be effective");

            // Test network security
            context_->logInfo("Testing network security");
            bool networkSecured = securityRuntime->testNetworkSecurity(containerId);
            context_->assertTrue(networkSecured, "Network should be properly secured");

            // Cleanup
            context_->assertTrue(securityRuntime->stopContainer(containerId), "Container should be stopped");
            context_->assertTrue(securityRuntime->removeContainer(containerId), "Container should be removed");

            context_->logInfo("Security integration test completed successfully");
        }

    private:
        std::shared_ptr<MockSecurityRuntime> createSecurityRuntime() {
            return std::make_shared<MockSecurityRuntime>();
        }

        class MockSecurityRuntime {
        private:
            std::unordered_map<std::string, std::unordered_map<std::string, std::string>> containers_;

        public:
            std::string createContainer(const std::string& image,
                                      const std::unordered_map<std::string, std::string>& config) {
                std::string containerId = "security-container-" + std::to_string(containers_.size());
                containers_[containerId] = config;
                containers_[containerId]["status"] = "created";
                return containerId;
            }

            bool startContainer(const std::string& containerId) {
                auto it = containers_.find(containerId);
                if (it != containers_.end()) {
                    it->second["status"] = "running";
                    return true;
                }
                return false;
            }

            bool testPrivilegeEscalation(const std::string& containerId) {
                return containers_.count(containerId) > 0;
            }

            bool testReadOnlyFilesystem(const std::string& containerId) {
                return containers_.count(containerId) > 0;
            }

            std::vector<std::string> getCapabilities(const std::string& containerId) {
                return {}; // No capabilities
            }

            bool isSeccompActive(const std::string& containerId) {
                return containers_.count(containerId) > 0;
            }

            size_t getMemoryLimit(const std::string& containerId) {
                return 256 * 1024 * 1024; // 256MB limit
            }

            bool testProcessIsolation(const std::string& containerId) {
                return containers_.count(containerId) > 0;
            }

            bool testNetworkSecurity(const std::string& containerId) {
                return containers_.count(containerId) > 0;
            }

            bool stopContainer(const std::string& containerId) {
                auto it = containers_.find(containerId);
                if (it != containers_.end()) {
                    it->second["status"] = "stopped";
                    return true;
                }
                return false;
            }

            bool removeContainer(const std::string& containerId) {
                return containers_.erase(containerId) > 0;
            }
        };
    };

    // Register all integration tests
    static std::vector<std::unique_ptr<ITest>> createTests() {
        std::vector<std::unique_ptr<ITest>> tests;

        tests.push_back(std::make_unique<EndToEndWorkflowTest>());
        tests.push_back(std::make_unique<MultiContainerNetworkingTest>());
        tests.push_back(std::make_unique<VolumeStorageIntegrationTest>());
        tests.push_back(std::make_unique<SecurityIntegrationTest>());

        return tests;
    }
};

} // namespace testing
```

## Chaos Engineering for Containers

### Chaos Testing Framework

```cpp
#include <random>
#include <atomic>
#include <chrono>

namespace testing {

// Chaos engineering fault types
enum class ChaosFault {
    NETWORK_PARTITION,
    CPU_PRESSURE,
    MEMORY_PRESSURE,
    DISK_FAILURE,
    PROCESS_KILL,
    DELAY_INJECTION,
    CORRUPTION_INJECTION,
    RESOURCE_STARVATION
};

// Chaos experiment configuration
struct ChaosConfig {
    ChaosFault fault_type;
    double probability = 0.1;  // Probability of fault injection
    std::chrono::milliseconds duration{5000};
    std::chrono::milliseconds delay{0};
    int severity = 1;  // 1-10 severity level
    std::unordered_map<std::string, std::string> parameters;
};

// Chaos experiment result
struct ChaosResult {
    bool success = false;
    std::string message;
    std::chrono::milliseconds actual_duration{0};
    std::unordered_map<std::string, std::string> metrics;
    std::vector<std::string> observations;
};

// Chaos experiment interface
class IChaosExperiment {
public:
    virtual ~IChaosExperiment() = default;

    virtual ChaosConfig getConfig() const = 0;
    virtual ChaosResult execute(TestContext* context) = 0;
    virtual void cleanup() = 0;
    virtual std::string getName() const = 0;
    virtual bool isSafeToRun() const = 0;
};

// Chaos engineering test base class
class ChaosTestBase : public TestBase {
protected:
    std::unique_ptr<ITestFixture> fixture_;
    std::vector<std::unique_ptr<IChaosExperiment>> experiments_;

public:
    ChaosTestBase(const std::string& name) : TestBase(name, "chaos") {
        metadata_.timeout = std::chrono::milliseconds(60000); // Long timeout for chaos tests
    }

    void beforeTest(TestContext* context) override {
        TestBase::beforeTest(context);
        fixture_ = std::make_unique<TestFixture>(metadata_.name);
        fixture_->setup();
    }

    void afterTest(TestContext* context) override {
        // Clean up all experiments
        for (auto& experiment : experiments_) {
            experiment->cleanup();
        }
        experiments_.clear();

        if (fixture_) {
            fixture_->teardown();
        }
        TestBase::afterTest(context);
    }

    void addExperiment(std::unique_ptr<IChaosExperiment> experiment) {
        experiments_.push_back(std::move(experiment));
    }

    template<typename T>
    T* getExperiment(const std::string& name) {
        for (auto& experiment : experiments_) {
            if (experiment->getName() == name) {
                return dynamic_cast<T*>(experiment.get());
            }
        }
        return nullptr;
    }
};

// Network partition chaos experiment
class NetworkPartitionExperiment : public IChaosExperiment {
private:
    ChaosConfig config_;
    std::vector<std::string> affected_containers_;
    bool partition_active_ = false;

public:
    NetworkPartitionExperiment(const std::string& networkName,
                              const std::vector<std::string>& containerIds,
                              double probability = 0.1)
        : config_({ChaosFault::NETWORK_PARTITION, probability}) {

        config_.parameters["network_name"] = networkName;
        affected_containers_ = containerIds;
    }

    ChaosConfig getConfig() const override {
        return config_;
    }

    ChaosResult execute(TestContext* context) override {
        context->logInfo("Starting network partition chaos experiment");

        ChaosResult result;
        result.success = true;

        try {
            // Simulate network partition
            partition_active_ = true;
            context->logInfo("Network partition activated for containers: " +
                           joinContainerIds(affected_containers_));

            // Wait for specified duration
            auto start_time = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now() - start_time < config_.duration) {
                if (partition_active_) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                } else {
                    break;
                }
            }

            // Check system resilience
            bool recovered = checkRecovery(context);
            if (!recovered) {
                result.success = false;
                result.message = "System failed to recover from network partition";
            }

            result.actual_duration = config_.duration;
            result.metrics["partition_duration"] = std::to_string(config_.duration.count());
            result.metrics["affected_containers"] = std::to_string(affected_containers_.size());

            context->logInfo("Network partition chaos experiment completed");

        } catch (const std::exception& e) {
            result.success = false;
            result.message = "Network partition experiment failed: " + std::string(e.what());
            context->logError(result.message);
        }

        return result;
    }

    void cleanup() override {
        if (partition_active_) {
            // Restore network connectivity
            partition_active_ = false;
        }
    }

    std::string getName() const override {
        return "NetworkPartition";
    }

    bool isSafeToRun() const override {
        // Check if it's safe to run this experiment
        return !affected_containers_.empty() && config_.probability > 0;
    }

private:
    std::string joinContainerIds(const std::vector<std::string>& containerIds) {
        std::string result;
        for (size_t i = 0; i < containerIds.size(); ++i) {
            if (i > 0) result += ", ";
            result += containerIds[i];
        }
        return result;
    }

    bool checkRecovery(TestContext* context) {
        // Simulate recovery check
        return true;
    }
};

// CPU pressure chaos experiment
class CpuPressureExperiment : public IChaosExperiment {
private:
    ChaosConfig config_;
    std::vector<std::thread> stress_threads_;
    std::atomic<bool> stress_active_{false};

public:
    CpuPressureExperiment(double cpuLoad = 0.8, double probability = 0.1)
        : config_({ChaosFault::CPU_PRESSURE, probability}) {

        config_.parameters["cpu_load"] = std::to_string(cpuLoad);
        config_.duration = std::chrono::milliseconds(10000);
    }

    ChaosConfig getConfig() const override {
        return config_;
    }

    ChaosResult execute(TestContext* context) override {
        context->logInfo("Starting CPU pressure chaos experiment");

        ChaosResult result;
        result.success = true;

        try {
            // Start CPU stress
            stress_active_ = true;
            int thread_count = std::thread::hardware_concurrency();
            double target_load = std::stod(config_.parameters["cpu_load"]);

            for (int i = 0; i < thread_count; ++i) {
                stress_threads_.emplace_back([this, target_load]() {
                    cpuStressWorker(target_load);
                });
            }

            context->logInfo("CPU pressure activated with " + std::to_string(thread_count) +
                           " threads targeting " + config_.parameters["cpu_load"] + " load");

            // Monitor system during stress
            auto start_time = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now() - start_time < config_.duration) {
                if (stress_active_) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                } else {
                    break;
                }
            }

            // Stop stress and check recovery
            stress_active_ = false;
            for (auto& thread : stress_threads_) {
                thread.join();
            }
            stress_threads_.clear();

            bool recovered = checkCpuRecovery(context);
            if (!recovered) {
                result.success = false;
                result.message = "System failed to recover from CPU pressure";
            }

            result.actual_duration = config_.duration;
            result.metrics["target_cpu_load"] = config_.parameters["cpu_load"];
            result.metrics["stress_threads"] = std::to_string(thread_count);

            context->logInfo("CPU pressure chaos experiment completed");

        } catch (const std::exception& e) {
            result.success = false;
            result.message = "CPU pressure experiment failed: " + std::string(e.what());
            context->logError(result.message);
            cleanup();
        }

        return result;
    }

    void cleanup() override {
        stress_active_ = false;
        for (auto& thread : stress_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        stress_threads_.clear();
    }

    std::string getName() const override {
        return "CpuPressure";
    }

    bool isSafeToRun() const override {
        return std::stod(config_.parameters["cpu_load"]) > 0.0 &&
               std::stod(config_.parameters["cpu_load"]) <= 1.0;
    }

private:
    void cpuStressWorker(double target_load) {
        auto start_time = std::chrono::steady_clock::now();
        auto busy_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::milliseconds(100) * target_load);

        while (stress_active_) {
            auto busy_start = std::chrono::steady_clock::now();
            auto busy_end = busy_start + busy_time;

            // Keep CPU busy
            while (std::chrono::steady_clock::now() < busy_end && stress_active_) {
                // Burn CPU cycles
                volatile int counter = 0;
                for (int i = 0; i < 1000; ++i) {
                    counter += i;
                }
            }

            // Sleep for remaining time
            if (stress_active_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100) - busy_time);
            }
        }
    }

    bool checkCpuRecovery(TestContext* context) {
        // Simulate CPU recovery check
        return true;
    }
};

// Memory pressure chaos experiment
class MemoryPressureExperiment : public IChaosExperiment {
private:
    ChaosConfig config_;
    std::vector<std::unique_ptr<char[]>> memory_allocations_;
    std::atomic<bool> allocation_active_{false};
    size_t total_allocated_{0};

public:
    MemoryPressureExperiment(size_t memoryMB = 512, double probability = 0.1)
        : config_({ChaosFault::MEMORY_PRESSURE, probability}) {

        config_.parameters["memory_mb"] = std::to_string(memoryMB);
        config_.duration = std::chrono::milliseconds(15000);
    }

    ChaosConfig getConfig() const override {
        return config_;
    }

    ChaosResult execute(TestContext* context) override {
        context->logInfo("Starting memory pressure chaos experiment");

        ChaosResult result;
        result.success = true;

        try {
            // Start memory allocation
            allocation_active_ = true;
            size_t target_memory = std::stoull(config_.parameters["memory_mb"]) * 1024 * 1024;
            const size_t chunk_size = 1024 * 1024; // 1MB chunks

            context->logInfo("Allocating " + config_.parameters["memory_mb"] + "MB of memory");

            while (allocation_active_ && total_allocated_ < target_memory) {
                try {
                    auto chunk = std::make_unique<char[]>(chunk_size);
                    // Write to ensure pages are actually allocated
                    memset(chunk.get(), 0xAA, chunk_size);
                    memory_allocations_.push_back(std::move(chunk));
                    total_allocated_ += chunk_size;

                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                } catch (const std::bad_alloc&) {
                    context->logWarning("Memory allocation failed - system under memory pressure");
                    break;
                }
            }

            context->logInfo("Memory pressure activated with " +
                           std::to_string(total_allocated_ / (1024 * 1024)) + "MB allocated");

            // Monitor system during memory pressure
            auto start_time = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now() - start_time < config_.duration) {
                if (allocation_active_) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                } else {
                    break;
                }
            }

            // Release memory and check recovery
            allocation_active_ = false;
            memory_allocations_.clear();
            total_allocated_ = 0;

            bool recovered = checkMemoryRecovery(context);
            if (!recovered) {
                result.success = false;
                result.message = "System failed to recover from memory pressure";
            }

            result.actual_duration = config_.duration;
            result.metrics["allocated_memory_mb"] = std::to_string(total_allocated_ / (1024 * 1024));
            result.metrics["target_memory_mb"] = config_.parameters["memory_mb"];

            context->logInfo("Memory pressure chaos experiment completed");

        } catch (const std::exception& e) {
            result.success = false;
            result.message = "Memory pressure experiment failed: " + std::string(e.what());
            context->logError(result.message);
            cleanup();
        }

        return result;
    }

    void cleanup() override {
        allocation_active_ = false;
        memory_allocations_.clear();
        total_allocated_ = 0;
    }

    std::string getName() const override {
        return "MemoryPressure";
    }

    bool isSafeToRun() const override {
        size_t memoryMB = std::stoull(config_.parameters["memory_mb"]);
        return memoryMB > 0 && memoryMB <= 4096; // Cap at 4GB for safety
    }

private:
    bool checkMemoryRecovery(TestContext* context) {
        // Simulate memory recovery check
        return true;
    }
};

// Process kill chaos experiment
class ProcessKillExperiment : public IChaosExperiment {
private:
    ChaosConfig config_;
    std::vector<std::string> target_processes_;
    std::vector<std::string> killed_processes_;

public:
    ProcessKillExperiment(const std::vector<std::string>& processes, double probability = 0.1)
        : config_({ChaosFault::PROCESS_KILL, probability}) {

        target_processes_ = processes;
        config_.duration = std::chrono::milliseconds(5000);
    }

    ChaosConfig getConfig() const override {
        return config_;
    }

    ChaosResult execute(TestContext* context) override {
        context->logInfo("Starting process kill chaos experiment");

        ChaosResult result;
        result.success = true;

        try {
            // Kill target processes
            context->logInfo("Killing processes: " + joinProcessNames(target_processes_));

            for (const auto& process : target_processes_) {
                if (shouldKillProcess(config_.probability)) {
                    if (killProcess(process)) {
                        killed_processes_.push_back(process);
                        context->logInfo("Killed process: " + process);
                    }
                }
            }

            // Wait for specified duration
            auto start_time = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now() - start_time < config_.duration) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            // Check process recovery
            bool recovered = checkProcessRecovery(context);
            if (!recovered) {
                result.success = false;
                result.message = "System failed to recover from process kills";
            }

            result.actual_duration = config_.duration;
            result.metrics["target_processes"] = std::to_string(target_processes_.size());
            result.metrics["killed_processes"] = std::to_string(killed_processes_.size());

            context->logInfo("Process kill chaos experiment completed");

        } catch (const std::exception& e) {
            result.success = false;
            result.message = "Process kill experiment failed: " + std::string(e.what());
            context->logError(result.message);
        }

        return result;
    }

    void cleanup() override {
        // Process cleanup happens automatically by the system
        killed_processes_.clear();
    }

    std::string getName() const override {
        return "ProcessKill";
    }

    bool isSafeToRun() const override {
        return !target_processes_.empty() && config_.probability > 0;
    }

private:
    std::string joinProcessNames(const std::vector<std::string>& processes) {
        std::string result;
        for (size_t i = 0; i < processes.size(); ++i) {
            if (i > 0) result += ", ";
            result += processes[i];
        }
        return result;
    }

    bool shouldKillProcess(double probability) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<> dis(0.0, 1.0);
        return dis(gen) < probability;
    }

    bool killProcess(const std::string& processName) {
        // Simulate process kill
        return true;
    }

    bool checkProcessRecovery(TestContext* context) {
        // Simulate process recovery check
        return true;
    }
};

// Chaos engineering test suite
class ChaosEngineeringTests {
public:
    // Network resilience chaos test
    class NetworkResilienceTest : public ChaosTestBase {
    public:
        NetworkResilienceTest() : ChaosTestBase("NetworkResilience") {}

        void runTest(TestContext* context) override {
            context_->logInfo("Starting network resilience chaos test");

            // Create test containers
            std::vector<std::string> containers = createTestContainers(context);

            // Add network partition experiment
            auto networkExp = std::make_unique<NetworkPartitionExperiment>(
                "test-network", containers, 0.3);
            addExperiment(std::move(networkExp));

            // Add CPU pressure experiment
            auto cpuExp = std::make_unique<CpuPressureExperiment>(0.6, 0.2);
            addExperiment(std::move(cpuExp));

            // Execute chaos experiments
            for (auto& experiment : experiments_) {
                if (!experiment->isSafeToRun()) {
                    context_->logWarning("Skipping unsafe experiment: " + experiment->getName());
                    continue;
                }

                auto result = experiment->execute(context);
                context_->assertTrue(result.success,
                    "Chaos experiment should succeed: " + experiment->getName() +
                    " - " + result.message);

                context_->logInfo("Experiment " + experiment->getName() +
                               " completed with result: " +
                               (result.success ? "SUCCESS" : "FAILURE"));
            }

            // Verify system recovery
            bool systemRecovered = verifySystemRecovery(context, containers);
            context_->assertTrue(systemRecovered, "System should recover from chaos experiments");

            // Cleanup test containers
            cleanupTestContainers(context, containers);

            context_->logInfo("Network resilience chaos test completed successfully");
        }

    private:
        std::vector<std::string> createTestContainers(TestContext* context) {
            // Simulate creating test containers
            return {"container-1", "container-2", "container-3"};
        }

        bool verifySystemRecovery(TestContext* context, const std::vector<std::string>& containers) {
            // Simulate system recovery verification
            return true;
        }

        void cleanupTestContainers(TestContext* context, const std::vector<std::string>& containers) {
            // Simulate cleaning up test containers
        }
    };

    // Resource pressure chaos test
    class ResourcePressureTest : public ChaosTestBase {
    public:
        ResourcePressureTest() : ChaosTestBase("ResourcePressure") {}

        void runTest(TestContext* context) override {
            context_->logInfo("Starting resource pressure chaos test");

            // Create test environment
            auto testEnv = createTestEnvironment(context);

            // Add memory pressure experiment
            auto memoryExp = std::make_unique<MemoryPressureExperiment>(1024, 0.25);
            addExperiment(std::move(memoryExp));

            // Add CPU pressure experiment
            auto cpuExp = std::make_unique<CpuPressureExperiment>(0.8, 0.3);
            addExperiment(std::move(cpuExp));

            // Add process kill experiment
            std::vector<std::string> criticalProcesses = {"dockerd", "containerd"};
            auto processExp = std::make_unique<ProcessKillExperiment>(criticalProcesses, 0.15);
            addExperiment(std::move(processExp));

            // Execute experiments in parallel to create combined stress
            std::vector<std::future<ChaosResult>> futures;
            for (auto& experiment : experiments_) {
                if (!experiment->isSafeToRun()) {
                    context_->logWarning("Skipping unsafe experiment: " + experiment->getName());
                    continue;
                }

                futures.push_back(std::async(std::launch::async,
                    [&experiment, context]() {
                        return experiment->execute(context);
                    }));
            }

            // Wait for all experiments to complete
            bool allSuccess = true;
            for (auto& future : futures) {
                auto result = future.get();
                if (!result.success) {
                    allSuccess = false;
                    context_->logError("Chaos experiment failed: " + result.message);
                }
            }

            context_->assertTrue(allSuccess, "All chaos experiments should succeed");

            // Verify system stability after combined stress
            bool stable = verifySystemStability(context);
            context_->assertTrue(stable, "System should remain stable after chaos experiments");

            // Cleanup test environment
            cleanupTestEnvironment(context, testEnv);

            context_->logInfo("Resource pressure chaos test completed successfully");
        }

    private:
        std::unordered_map<std::string, std::string> createTestEnvironment(TestContext* context) {
            return {}; // Simulate test environment creation
        }

        bool verifySystemStability(TestContext* context) {
            // Simulate system stability verification
            return true;
        }

        void cleanupTestEnvironment(TestContext* context,
                                  const std::unordered_map<std::string, std::string>& env) {
            // Simulate test environment cleanup
        }
    };

    // Register all chaos tests
    static std::vector<std::unique_ptr<ITest>> createTests() {
        std::vector<std::unique_ptr<ITest>> tests;

        tests.push_back(std::make_unique<NetworkResilienceTest>());
        tests.push_back(std::make_unique<ResourcePressureTest>());

        return tests;
    }
};

} // namespace testing
```

## Complete Implementation

### Test Runner and Reporting System

```cpp
#include <fstream>
#include <iomanip>

namespace testing {

// Test suite implementation
class TestSuite : public ITestSuite {
private:
    std::string name_;
    std::vector<std::unique_ptr<ITest>> tests_;
    std::vector<std::unique_ptr<ITestSuite>> suites_;

public:
    TestSuite(const std::string& name) : name_(name) {}

    std::string getName() const override {
        return name_;
    }

    void addTest(std::unique_ptr<ITest> test) override {
        tests_.push_back(std::move(test));
    }

    void addSuite(std::unique_ptr<ITestSuite> suite) override {
        suites_.push_back(std::move(suite));
    }

    const std::vector<std::unique_ptr<ITest>>& getTests() const override {
        return tests_;
    }

    const std::vector<std::unique_ptr<ITestSuite>>& getSuites() const override {
        return suites_;
    }

    void beforeAll(TestContext* context) override {
        context->logInfo("Starting test suite: " + name_);
    }

    void afterAll(TestContext* context) override {
        context->logInfo("Completed test suite: " + name_);
    }
};

// Console test reporter
class ConsoleTestReporter : public ITestReporter {
private:
    size_t total_tests_ = 0;
    size_t passed_tests_ = 0;
    size_t failed_tests_ = 0;
    size_t skipped_tests_ = 0;

public:
    void reportTestStart(const TestMetadata& metadata) override {
        std::cout << "[ RUN      ] " << metadata.suite << "." << metadata.name << std::endl;
        total_tests_++;
    }

    void reportTestEnd(const TestMetadata& metadata, TestResult result,
                     const std::string& message, std::chrono::milliseconds duration) override {
        switch (result) {
            case TestResult::PASSED:
                std::cout << "[       OK ] " << metadata.suite << "." << metadata.name
                          << " (" << duration.count() << "ms)" << std::endl;
                passed_tests_++;
                break;
            case TestResult::FAILED:
                std::cout << "[  FAILED  ] " << metadata.suite << "." << metadata.name
                          << " (" << duration.count() << "ms)" << std::endl;
                std::cout << "            " << message << std::endl;
                failed_tests_++;
                break;
            case TestResult::SKIPPED:
                std::cout << "[  SKIPPED ] " << metadata.suite << "." << metadata.name << std::endl;
                skipped_tests_++;
                break;
            case TestResult::TIMEOUT:
                std::cout << "[  TIMEOUT ] " << metadata.suite << "." << metadata.name
                          << " (" << duration.count() << "ms)" << std::endl;
                failed_tests_++;
                break;
            case TestResult::ERROR:
                std::cout << "[   ERROR  ] " << metadata.suite << "." << metadata.name
                          << " (" << duration.count() << "ms)" << std::endl;
                std::cout << "            " << message << std::endl;
                failed_tests_++;
                break;
        }
    }

    void reportSuiteStart(const std::string& suiteName) override {
        std::cout << "[----------] " << suiteName << std::endl;
    }

    void reportSuiteEnd(const std::string& suiteName) override {
        std::cout << "[----------] " << suiteName << " completed" << std::endl;
        std::cout << std::endl;
    }

    void reportSummary(size_t total, size_t passed, size_t failed, size_t skipped) override {
        std::cout << "[==========] " << total << " tests from all test suites ran." << std::endl;
        std::cout << "[  PASSED  ] " << passed << " tests." << std::endl;
        if (failed > 0) {
            std::cout << "[  FAILED  ] " << failed << " tests, listed below:" << std::endl;
        }
        if (skipped > 0) {
            std::cout << "[  SKIPPED ] " << skipped << " tests." << std::endl;
        }

        if (failed == 0) {
            std::cout << std::endl << "You look great today!" << std::endl;
        }
    }

    void reportError(const std::string& error) override {
        std::cerr << "[   ERROR  ] " << error << std::endl;
    }
};

// JUnit XML test reporter
class JUnitTestReporter : public ITestReporter {
private:
    std::ofstream xml_file_;
    std::string current_suite_;
    std::chrono::steady_clock::time_point suite_start_time_;
    std::vector<std::string> test_cases_;

public:
    JUnitTestReporter(const std::string& filename) {
        xml_file_.open(filename);
        if (xml_file_.is_open()) {
            xml_file_ << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" << std::endl;
            xml_file_ << "<testsuites>" << std::endl;
        }
    }

    ~JUnitTestReporter() {
        if (xml_file_.is_open()) {
            xml_file_ << "</testsuites>" << std::endl;
            xml_file_.close();
        }
    }

    void reportTestStart(const TestMetadata& metadata) override {
        std::string test_case = "    <testcase name=\"" + metadata.name + "\"";
        test_case += " classname=\"" + metadata.suite + "\"";
        test_case += " time=\"" + std::to_string(metadata.timeout.count() / 1000.0) + "\">";
        test_cases_.push_back(test_case);
    }

    void reportTestEnd(const TestMetadata& metadata, TestResult result,
                     const std::string& message, std::chrono::milliseconds duration) override {
        if (!xml_file_.is_open()) return;

        if (!test_cases_.empty()) {
            std::string test_case = test_cases_.back();
            test_cases_.pop_back();

            switch (result) {
                case TestResult::FAILED:
                case TestResult::TIMEOUT:
                case TestResult::ERROR:
                    test_case += "\n        <failure message=\"" + escapeXml(message) + "\"/>";
                    break;
                case TestResult::SKIPPED:
                    test_case += "\n        <skipped/>";
                    break;
                default:
                    break;
            }

            test_case += "\n    </testcase>";
            xml_file_ << test_case << std::endl;
        }
    }

    void reportSuiteStart(const std::string& suiteName) override {
        if (!xml_file_.is_open()) return;

        current_suite_ = suiteName;
        suite_start_time_ = std::chrono::steady_clock::now();
    }

    void reportSuiteEnd(const std::string& suiteName) override {
        if (!xml_file_.is_open()) return;

        auto duration = std::chrono::steady_clock::now() - suite_start_time_;
        auto duration_seconds = std::chrono::duration_cast<std::chrono::milliseconds>(duration);

        xml_file_ << "    <testsuite name=\"" << suiteName << "\""
                  << " time=\"" << std::to_string(duration_seconds.count() / 1000.0) << "\">"
                  << std::endl;

        // Add test cases (already written)
        xml_file_ << "    </testsuite>" << std::endl;
    }

    void reportSummary(size_t total, size_t passed, size_t failed, size_t skipped) override {
        // Summary handled in destructor
    }

    void reportError(const std::string& error) override {
        if (!xml_file_.is_open()) return;
        xml_file_ << "<error message=\"" << escapeXml(error) << "\"/>" << std::endl;
    }

private:
    std::string escapeXml(const std::string& input) {
        std::string result = input;
        size_t pos = 0;
        while ((pos = result.find('&', pos)) != std::string::npos) {
            result.replace(pos, 1, "&amp;");
            pos += 5;
        }
        pos = 0;
        while ((pos = result.find('<', pos)) != std::string::npos) {
            result.replace(pos, 1, "&lt;");
            pos += 4;
        }
        pos = 0;
        while ((pos = result.find('>', pos)) != std::string::npos) {
            result.replace(pos, 1, "&gt;");
            pos += 4;
        }
        pos = 0;
        while ((pos = result.find('"', pos)) != std::string::npos) {
            result.replace(pos, 1, "&quot;");
            pos += 6;
        }
        pos = 0;
        while ((pos = result.find('\'', pos)) != std::string::npos) {
            result.replace(pos, 1, "&apos;");
            pos += 6;
        }
        return result;
    }
};

// Test runner implementation
class TestRunner : public ITestRunner {
private:
    std::shared_ptr<ITestReporter> reporter_;
    std::chrono::milliseconds timeout_{5000};
    std::chrono::milliseconds global_timeout_{300000}; // 5 minutes

public:
    TestRunner() {
        reporter_ = std::make_shared<ConsoleTestReporter>();
    }

    void setReporter(std::shared_ptr<ITestReporter> reporter) override {
        reporter_ = reporter;
    }

    void setTimeout(std::chrono::milliseconds timeout) override {
        timeout_ = timeout;
    }

    bool run(ITest* test, TestContext* context) override {
        auto metadata = test->getMetadata();
        reporter_->reportTestStart(metadata);

        auto start_time = std::chrono::steady_clock::now();
        TestResult result = TestResult::ERROR;
        std::string message;

        // Check if test should run
        if (!metadata.enabled) {
            result = TestResult::SKIPPED;
            message = "Test disabled";
        } else {
            // Run test with timeout
            std::future<TestResult> future = std::async(std::launch::async,
                [this, test, context, &message]() {
                    try {
                        test->beforeTest(context);
                        TestResult result = test->execute(context);
                        test->afterTest(context);
                        return result;
                    } catch (const std::exception& e) {
                        message = e.what();
                        return TestResult::ERROR;
                    } catch (...) {
                        message = "Unknown exception";
                        return TestResult::ERROR;
                    }
                });

            auto test_timeout = metadata.timeout > std::chrono::milliseconds(0) ?
                               metadata.timeout : timeout_;

            if (future.wait_for(test_timeout) == std::future_status::timeout) {
                result = TestResult::TIMEOUT;
                message = "Test timed out after " + std::to_string(test_timeout.count()) + "ms";
            } else {
                result = future.get();
            }
        }

        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time);

        reporter_->reportTestEnd(metadata, result, message, duration);
        return result == TestResult::PASSED;
    }

    bool run(ITestSuite* suite, TestContext* context) override {
        reporter_->reportSuiteStart(suite->getName());

        auto start_time = std::chrono::steady_clock::now();

        // Run beforeAll hook
        try {
            suite->beforeAll(context);
        } catch (const std::exception& e) {
            reporter_->reportError("Suite beforeAll failed: " + std::string(e.what()));
            return false;
        }

        bool all_passed = true;
        size_t total = 0;
        size_t passed = 0;
        size_t failed = 0;
        size_t skipped = 0;

        // Run nested suites
        for (auto& nested_suite : suite->getSuites()) {
            bool suite_passed = run(nested_suite.get(), context);
            if (!suite_passed) {
                all_passed = false;
            }
        }

        // Run tests in this suite
        for (auto& test : suite->getTests()) {
            auto metadata = test->getMetadata();
            total++;

            if (run(test.get(), context)) {
                passed++;
            } else {
                failed++;
                all_passed = false;
            }
        }

        // Run afterAll hook
        try {
            suite->afterAll(context);
        } catch (const std::exception& e) {
            reporter_->reportError("Suite afterAll failed: " + std::string(e.what()));
            all_passed = false;
        }

        reporter_->reportSuiteEnd(suite->getName());
        return all_passed;
    }

    // Run all tests with global timeout
    bool runWithGlobalTimeout(ITestSuite* suite, TestContext* context) {
        auto future = std::async(std::launch::async,
            [this, suite, context]() {
                return run(suite, context);
            });

        if (future.wait_for(global_timeout_) == std::future_status::timeout) {
            reporter_->reportError("Test execution timed out after " +
                                std::to_string(global_timeout_.count()) + "ms");
            return false;
        }

        return future.get();
    }
};

// Test main function
int runTests(int argc, char* argv[]) {
    try {
        // Parse command line arguments
        std::string output_format = "console";
        std::string output_file = "";
        std::string test_filter = "";
        std::chrono::milliseconds timeout(5000);

        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--format=junit" && i + 1 < argc) {
                output_format = "junit";
                output_file = argv[++i];
            } else if (arg == "--filter" && i + 1 < argc) {
                test_filter = argv[++i];
            } else if (arg == "--timeout" && i + 1 < argc) {
                timeout = std::chrono::milliseconds(std::stoi(argv[++i]));
            }
        }

        // Create test runner
        auto runner = std::make_unique<TestRunner>();
        runner->setTimeout(timeout);

        // Set reporter based on format
        if (output_format == "junit") {
            auto reporter = std::make_shared<JUnitTestReporter>(output_file);
            runner->setReporter(reporter);
        }

        // Create test context
        auto context = std::make_unique<ConcreteTestContext>();
        context->setup();

        // Create master test suite
        auto master_suite = std::make_unique<TestSuite>("DockerCppTests");

        // Add all test suites
        auto unit_suite = std::make_unique<TestSuite>("UnitTests");
        for (auto& test : ContainerRuntimeTests::createTests()) {
            unit_suite->addTest(std::move(test));
        }
        master_suite->addSuite(std::move(unit_suite));

        auto integration_suite = std::make_unique<TestSuite>("IntegrationTests");
        for (auto& test : ContainerIntegrationTests::createTests()) {
            integration_suite->addTest(std::move(test));
        }
        master_suite->addSuite(std::move(integration_suite));

        auto chaos_suite = std::make_unique<TestSuite>("ChaosTests");
        for (auto& test : ChaosEngineeringTests::createTests()) {
            chaos_suite->addTest(std::move(test));
        }
        master_suite->addSuite(std::move(chaos_suite));

        // Run tests
        bool success = runner->runWithGlobalTimeout(master_suite.get(), context.get());

        // Cleanup
        context->teardown();

        return success ? 0 : 1;

    } catch (const std::exception& e) {
        std::cerr << "Test runner failed: " << e.what() << std::endl;
        return 1;
    }
}

} // namespace testing

// Main function
int main(int argc, char* argv[]) {
    return testing::runTests(argc, argv);
}
```

## Usage Examples

### Running the Test Suite

```cpp
#include "test_framework.h"

int main(int argc, char* argv[]) {
    // Set up environment
    testing::TestEnvironment env;
    env.initialize();

    // Create test configuration
    testing::TestConfig config;
    config.timeout = std::chrono::milliseconds(10000);
    config.global_timeout = std::chrono::minutes(10);
    config.output_format = "console";
    config.parallel_execution = true;

    // Run tests
    auto result = testing::run_tests(config, argc, argv);

    // Cleanup
    env.cleanup();

    return result.success ? 0 : 1;
}
```

### Custom Test Example

```cpp
class MyCustomTest : public testing::TestBase {
public:
    MyCustomTest() : TestBase("MyCustomTest", "custom") {}

    void runTest(testing::TestContext* context) override {
        // Test setup
        auto container = createTestContainer(context);
        auto volume = createTestVolume(context);

        // Test execution
        bool result = container->start();
        context->assertTrue(result, "Container should start");

        // Test verification
        auto status = container->getStatus();
        context->assertEquals("running", status);

        // Cleanup
        container->stop();
        volume->remove();
    }

private:
    std::shared_ptr<MockContainer> createTestContainer(testing::TestContext* context) {
        auto container = std::make_shared<MockContainer>();
        addCleanupAction([container]() { container->cleanup(); });
        return container;
    }

    std::shared_ptr<MockVolume> createTestVolume(testing::TestContext* context) {
        auto volume = std::make_shared<MockVolume>();
        addCleanupAction([volume]() { volume->cleanup(); });
        return volume;
    }
};
```

## Best Practices

### Testing Best Practices

1. **Test Organization**:
   - Organize tests by feature and layer (unit, integration, chaos)
   - Use descriptive test names that explain what is being tested
   - Group related tests in test suites

2. **Test Isolation**:
   - Each test should be independent and not rely on test execution order
   - Clean up resources after each test
   - Use fresh fixtures for each test

3. **Assertion Strategy**:
   - Use specific assertions for different types of validation
   - Include meaningful error messages in assertions
   - Assert what you're actually testing, not implementation details

4. **Mock Objects**:
   - Mock external dependencies to isolate the system under test
   - Verify mock interactions to ensure proper integration
   - Use realistic mock behavior that matches real dependencies

5. **Performance Testing**:
   - Set up performance baselines and track changes
   - Test under realistic load conditions
   - Monitor resource usage during tests

6. **Chaos Engineering**:
   - Start with low probability and severity
   - Focus on critical failure scenarios
   - Ensure experiments can be safely stopped

7. **Continuous Integration**:
   - Run fast unit tests on every commit
   - Run integration tests daily or on significant changes
   - Run chaos tests periodically but not on every commit

This comprehensive testing framework provides a solid foundation for ensuring the reliability, performance, and resilience of container runtime systems through systematic testing across all levels of the system.
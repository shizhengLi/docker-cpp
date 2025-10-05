#include <gtest/gtest.h>
#include <chrono>
#include <memory>
#include <thread>
#include "runtime/container_runtime.hpp"

using namespace dockercpp::runtime;

class ContainerRuntimeTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        runtime_ = ContainerRuntimeFactory::createRuntime();
        ASSERT_NE(runtime_, nullptr);
    }

    void TearDown() override
    {
        if (runtime_) {
            runtime_->shutdown();
        }
    }

    std::unique_ptr<ContainerRuntime> runtime_;
};

TEST_F(ContainerRuntimeTest, CreateRuntime)
{
    // Runtime should be created successfully
    EXPECT_NE(runtime_, nullptr);

    // Check initial state
    EXPECT_EQ(runtime_->getContainerCount(), 0);
    EXPECT_EQ(runtime_->getRunningContainerCount(), 0);

    auto containers = runtime_->listContainers();
    EXPECT_TRUE(containers.empty());
}

TEST_F(ContainerRuntimeTest, CreateContainerBasic)
{
    ContainerConfig config;
    config.name = "test-container";
    config.image = "ubuntu:latest";
    config.command = {"/bin/echo", "hello world"};
    config.working_dir = "/app";

    std::string container_id = runtime_->createContainer(config);

    EXPECT_FALSE(container_id.empty());
    EXPECT_TRUE(isValidContainerId(container_id));

    // Check container was created
    EXPECT_EQ(runtime_->getContainerCount(), 1);
    EXPECT_EQ(runtime_->getRunningContainerCount(), 0);

    auto containers = runtime_->listContainers();
    EXPECT_EQ(containers.size(), 1);
    EXPECT_EQ(containers[0].id, container_id);
    EXPECT_EQ(containers[0].name, "test-container");
    EXPECT_EQ(containers[0].state, ContainerState::CREATED);
}

TEST_F(ContainerRuntimeTest, CreateContainerWithDefaults)
{
    ContainerConfig config = runtime_utils::createDefaultConfig("ubuntu:latest");
    config.name = "default-test";

    std::string container_id = runtime_->createContainer(config);

    EXPECT_FALSE(container_id.empty());

    auto info = runtime_->inspectContainer(container_id);
    EXPECT_EQ(info.image, "ubuntu:latest");
    EXPECT_EQ(info.state, ContainerState::CREATED);
    EXPECT_GT(info.config.resources.cpu_shares, 0);
}

TEST_F(ContainerRuntimeTest, CreateContainerValidation)
{
    // Test invalid configuration
    ContainerConfig config; // Missing image

    EXPECT_THROW(runtime_->createContainer(config), ContainerConfigurationError);

    // Test invalid name
    config.image = "ubuntu:latest";
    config.name = "invalid@name";

    EXPECT_THROW(runtime_->createContainer(config), ContainerConfigurationError);
}

TEST_F(ContainerRuntimeTest, StartContainer)
{
    ContainerConfig config;
    config.name = "start-test";
    config.image = "ubuntu:latest";
    config.command = {"/bin/sleep", "1"};

    std::string container_id = runtime_->createContainer(config);
    EXPECT_EQ(runtime_->getContainerState(container_id), ContainerState::CREATED);

    runtime_->startContainer(container_id);
    EXPECT_EQ(runtime_->getContainerState(container_id), ContainerState::RUNNING);
    EXPECT_EQ(runtime_->getRunningContainerCount(), 1);

    // Wait for container to finish
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    // Container should be stopped
    auto state = runtime_->getContainerState(container_id);
    EXPECT_TRUE(state == ContainerState::STOPPED || state == ContainerState::EXITED);
}

TEST_F(ContainerRuntimeTest, StopContainer)
{
    ContainerConfig config;
    config.name = "stop-test";
    config.image = "ubuntu:latest";
    config.command = {"/bin/sleep", "10"};

    std::string container_id = runtime_->createContainer(config);
    runtime_->startContainer(container_id);

    EXPECT_EQ(runtime_->getContainerState(container_id), ContainerState::RUNNING);

    runtime_->stopContainer(container_id, 5);

    // Wait a bit for stop to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    auto state = runtime_->getContainerState(container_id);
    EXPECT_TRUE(state == ContainerState::STOPPED || state == ContainerState::EXITED);
}

TEST_F(ContainerRuntimeTest, RemoveContainer)
{
    ContainerConfig config;
    config.name = "remove-test";
    config.image = "ubuntu:latest";
    config.command = {"/bin/echo", "test"};

    std::string container_id = runtime_->createContainer(config);
    EXPECT_EQ(runtime_->getContainerCount(), 1);

    // Cannot remove running container without force
    runtime_->startContainer(container_id);
    EXPECT_THROW(runtime_->removeContainer(container_id), InvalidContainerStateError);

    // Wait for container to finish
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Remove stopped container
    runtime_->removeContainer(container_id);
    EXPECT_EQ(runtime_->getContainerCount(), 0);

    // Container should no longer exist
    EXPECT_THROW(runtime_->inspectContainer(container_id), ContainerNotFoundError);
}

TEST_F(ContainerRuntimeTest, ForceRemoveContainer)
{
    ContainerConfig config;
    config.name = "force-remove-test";
    config.image = "ubuntu:latest";
    config.command = {"/bin/sleep", "10"};

    std::string container_id = runtime_->createContainer(config);
    runtime_->startContainer(container_id);

    EXPECT_EQ(runtime_->getContainerState(container_id), ContainerState::RUNNING);

    // Force remove running container
    runtime_->removeContainer(container_id, true);
    EXPECT_EQ(runtime_->getContainerCount(), 0);
}

TEST_F(ContainerRuntimeTest, PauseResumeContainer)
{
    ContainerConfig config;
    config.name = "pause-test";
    config.image = "ubuntu:latest";
    config.command = {"/bin/sleep", "5"};

    std::string container_id = runtime_->createContainer(config);
    runtime_->startContainer(container_id);

    EXPECT_EQ(runtime_->getContainerState(container_id), ContainerState::RUNNING);

    // Pause container
    runtime_->pauseContainer(container_id);
    EXPECT_EQ(runtime_->getContainerState(container_id), ContainerState::PAUSED);

    // Resume container
    runtime_->resumeContainer(container_id);
    EXPECT_EQ(runtime_->getContainerState(container_id), ContainerState::RUNNING);
}

TEST_F(ContainerRuntimeTest, RestartContainer)
{
    ContainerConfig config;
    config.name = "restart-test";
    config.image = "ubuntu:latest";
    config.command = {"/bin/sh", "-c", "echo 'test'; sleep 1"};

    std::string container_id = runtime_->createContainer(config);
    runtime_->startContainer(container_id);

    // Wait for first run to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    auto info1 = runtime_->inspectContainer(container_id);
    EXPECT_EQ(info1.exit_code, 0);

    // Restart container
    runtime_->restartContainer(container_id);
    EXPECT_EQ(runtime_->getContainerState(container_id), ContainerState::RUNNING);

    // Wait for restart to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    auto info2 = runtime_->inspectContainer(container_id);
    EXPECT_EQ(info2.exit_code, 0);
    EXPECT_GT(info2.started_at, info1.started_at);
}

TEST_F(ContainerRuntimeTest, ListContainers)
{
    // Create multiple containers
    std::vector<std::string> container_ids;

    for (int i = 0; i < 3; ++i) {
        ContainerConfig config;
        config.name = "list-test-" + std::to_string(i);
        config.image = "ubuntu:latest";
        config.command = {"/bin/echo", "test " + std::to_string(i)};

        std::string id = runtime_->createContainer(config);
        container_ids.push_back(id);
    }

    // List all containers
    auto all_containers = runtime_->listContainers(true);
    EXPECT_EQ(all_containers.size(), 3);

    // Start one container
    runtime_->startContainer(container_ids[0]);

    // List running containers only
    auto running_containers = runtime_->listContainers(false);
    EXPECT_EQ(running_containers.size(), 1);
    EXPECT_EQ(running_containers[0].id, container_ids[0]);

    // List container IDs
    auto all_ids = runtime_->listContainerIds(true);
    EXPECT_EQ(all_ids.size(), 3);

    auto running_ids = runtime_->listContainerIds(false);
    EXPECT_EQ(running_ids.size(), 1);
    EXPECT_EQ(running_ids[0], container_ids[0]);
}

TEST_F(ContainerRuntimeTest, InspectContainer)
{
    ContainerConfig config;
    config.name = "inspect-test";
    config.image = "ubuntu:latest";
    config.command = {"/bin/echo", "test"};
    config.env = {"TEST_VAR=test_value"};
    config.labels = {"version", "1.0"};
    config.working_dir = "/app";

    std::string container_id = runtime_->createContainer(config);

    auto info = runtime_->inspectContainer(container_id);

    EXPECT_EQ(info.id, container_id);
    EXPECT_EQ(info.name, "inspect-test");
    EXPECT_EQ(info.image, "ubuntu:latest");
    EXPECT_EQ(info.state, ContainerState::CREATED);
    EXPECT_EQ(info.config.command, config.command);
    EXPECT_EQ(info.config.env, config.env);
    EXPECT_EQ(info.config.labels, config.labels);
    EXPECT_EQ(info.config.working_dir, config.working_dir);
    EXPECT_GT(info.config.created.time_since_epoch().count(), 0);

    // Non-existent container should throw
    EXPECT_THROW(runtime_->inspectContainer("non_existent_id"), ContainerNotFoundError);
}

TEST_F(ContainerRuntimeTest, WaitForContainer)
{
    ContainerConfig config;
    config.name = "wait-test";
    config.image = "ubuntu:latest";
    config.command = {"/bin/sleep", "1"};

    std::string container_id = runtime_->createContainer(config);
    runtime_->startContainer(container_id);

    // Wait for container to stop
    auto start = std::chrono::high_resolution_clock::now();
    runtime_->waitForContainer(container_id, ContainerState::STOPPED, 5);
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    EXPECT_GE(duration.count(), 900);  // Should wait at least 1 second
    EXPECT_LE(duration.count(), 2000); // But not too long

    EXPECT_EQ(runtime_->getContainerState(container_id), ContainerState::STOPPED);
}

TEST_F(ContainerRuntimeTest, UpdateContainerResources)
{
    ContainerConfig config;
    config.name = "resource-test";
    config.image = "ubuntu:latest";
    config.command = {"/bin/sleep", "5"};

    std::string container_id = runtime_->createContainer(config);
    runtime_->startContainer(container_id);

    // Update resource limits
    ResourceLimits limits;
    limits.memory_limit = 256 * 1024 * 1024; // 256MB
    limits.cpu_shares = 2.0;

    runtime_->updateContainerResources(container_id, limits);

    // Verify limits were applied
    auto info = runtime_->inspectContainer(container_id);
    EXPECT_EQ(info.config.resources.memory_limit, limits.memory_limit);
    EXPECT_EQ(info.config.resources.cpu_shares, limits.cpu_shares);
}

TEST_F(ContainerRuntimeTest, GetContainerStats)
{
    ContainerConfig config;
    config.name = "stats-test";
    config.image = "ubuntu:latest";
    config.command = {"/bin/sleep", "2"};

    std::string container_id = runtime_->createContainer(config);
    runtime_->startContainer(container_id);

    // Get container statistics
    auto stats = runtime_->getContainerStats(container_id);
    EXPECT_GE(stats.memory_usage_bytes, 0);
    EXPECT_GE(stats.cpu_usage_percent, 0.0);
    EXPECT_GT(stats.timestamp.time_since_epoch().count(), 0);

    // Get all container statistics
    auto all_stats = runtime_->getAllContainerStats();
    EXPECT_EQ(all_stats.size(), 1);
    EXPECT_EQ(all_stats[0].memory_usage_bytes, stats.memory_usage_bytes);

    // Get aggregated statistics
    auto aggregated = runtime_->getAggregatedStats();
    EXPECT_EQ(aggregated.memory_usage_bytes, stats.memory_usage_bytes);
}

TEST_F(ContainerRuntimeTest, SystemInfo)
{
    auto info = runtime_->getSystemInfo();

    EXPECT_EQ(info.total_containers, 0);
    EXPECT_EQ(info.running_containers, 0);
    EXPECT_EQ(info.paused_containers, 0);
    EXPECT_EQ(info.stopped_containers, 0);
    EXPECT_FALSE(info.version.empty());
    EXPECT_FALSE(info.kernel_version.empty());
    EXPECT_FALSE(info.operating_system.empty());
    EXPECT_GT(info.system_time.time_since_epoch().count(), 0);
}

TEST_F(ContainerRuntimeTest, RuntimeConfiguration)
{
    ContainerRuntime::RuntimeConfig config;
    config.default_runtime = "runc";
    config.cgroup_driver = "systemd";
    config.storage_driver = "overlay2";
    config.default_memory_limit = 512 * 1024 * 1024; // 512MB
    config.default_cpu_shares = 2.0;

    runtime_->setRuntimeConfig(config);

    auto retrieved_config = runtime_->getRuntimeConfig();
    EXPECT_EQ(retrieved_config.default_runtime, "runc");
    EXPECT_EQ(retrieved_config.cgroup_driver, "systemd");
    EXPECT_EQ(retrieved_config.storage_driver, "overlay2");
    EXPECT_EQ(retrieved_config.default_memory_limit, 512 * 1024 * 1024);
    EXPECT_EQ(retrieved_config.default_cpu_shares, 2.0);
}

TEST_F(ContainerRuntimeTest, EventSubscription)
{
    std::vector<std::pair<std::string, std::map<std::string, std::string>>> received_events;

    // Subscribe to container events
    runtime_->subscribeToEvents([&received_events](const std::string& event_type,
                                                   const std::map<std::string, std::string>& data) {
        received_events.emplace_back(event_type, data);
    });

    // Create and start a container
    ContainerConfig config;
    config.name = "event-test";
    config.image = "ubuntu:latest";
    config.command = {"/bin/echo", "test"};

    std::string container_id = runtime_->createContainer(config);
    runtime_->startContainer(container_id);

    // Wait a bit for events to be processed
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Should have received container create and start events
    EXPECT_GE(received_events.size(), 2);

    // Find create event
    bool create_event_found = false;
    bool start_event_found = false;

    for (const auto& [event_type, data] : received_events) {
        if (event_type == "container.create") {
            create_event_found = true;
            EXPECT_EQ(data.at("container_id"), container_id);
        }
        else if (event_type == "container.start") {
            start_event_found = true;
            EXPECT_EQ(data.at("container_id"), container_id);
        }
    }

    EXPECT_TRUE(create_event_found);
    EXPECT_TRUE(start_event_found);

    // Unsubscribe from events
    runtime_->unsubscribeFromEvents();
}

TEST_F(ContainerRuntimeTest, GlobalOperations)
{
    // Create multiple containers
    std::vector<std::string> container_ids;

    for (int i = 0; i < 3; ++i) {
        ContainerConfig config;
        config.name = "global-test-" + std::to_string(i);
        config.image = "ubuntu:latest";
        config.command = {"/bin/sleep", "5"};

        std::string id = runtime_->createContainer(config);
        runtime_->startContainer(id);
        container_ids.push_back(id);
    }

    EXPECT_EQ(runtime_->getRunningContainerCount(), 3);

    // Pause all containers
    runtime_->pauseAllContainers();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    EXPECT_EQ(runtime_->getRunningContainerCount(),
              0); // Paused containers aren't counted as running

    // Resume all containers
    runtime_->resumeAllContainers();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    EXPECT_EQ(runtime_->getRunningContainerCount(), 3);

    // Stop all containers
    runtime_->stopAllContainers(5);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    EXPECT_EQ(runtime_->getRunningContainerCount(), 0);

    // Remove stopped containers
    runtime_->removeStoppedContainers();
    EXPECT_EQ(runtime_->getContainerCount(), 0);
}

TEST_F(ContainerRuntimeTest, RuntimeHealth)
{
    // Runtime should be healthy initially
    EXPECT_TRUE(runtime_->isHealthy());

    auto health_checks = runtime_->getHealthChecks();
    EXPECT_FALSE(health_checks.empty());

    // Perform maintenance
    runtime_->performMaintenance();
    EXPECT_TRUE(runtime_->isHealthy());
}

// ContainerRuntimeFactory tests
class ContainerRuntimeFactoryTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // Check if runtime environment is valid
        if (!ContainerRuntimeFactory::validateRuntimeEnvironment()) {
            GTEST_SKIP() << "Runtime environment not suitable for testing";
        }
    }
};

TEST_F(ContainerRuntimeFactoryTest, CreateDefaultRuntime)
{
    auto runtime = ContainerRuntimeFactory::createRuntime();
    EXPECT_NE(runtime, nullptr);

    runtime->shutdown();
}

TEST_F(ContainerRuntimeFactoryTest, CreateRuntimeWithConfig)
{
    ContainerRuntime::RuntimeConfig config;
    config.default_runtime = "runc";
    config.storage_driver = "overlay2";

    auto runtime = ContainerRuntimeFactory::createRuntime(config);
    EXPECT_NE(runtime, nullptr);

    auto retrieved_config = runtime->getRuntimeConfig();
    EXPECT_EQ(retrieved_config.default_runtime, "runc");
    EXPECT_EQ(retrieved_config.storage_driver, "overlay2");

    runtime->shutdown();
}

TEST_F(ContainerRuntimeFactoryTest, ValidateRuntimeEnvironment)
{
    bool is_valid = ContainerRuntimeFactory::validateRuntimeEnvironment();
    // This test may pass or fail depending on the environment
    // We just check that the method runs without crashing
}

TEST_F(ContainerRuntimeFactoryTest, GetSystemRequirements)
{
    auto requirements = ContainerRuntimeFactory::getSystemRequirements();
    EXPECT_FALSE(requirements.empty());
}

TEST_F(ContainerRuntimeFactoryTest, ValidateSystemConfiguration)
{
    auto issues = ContainerRuntimeFactory::validateSystemConfiguration();
    // May return empty or some validation issues
    // We just check that the method runs without crashing
}

// Runtime utility tests
class RuntimeUtilsTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        runtime_ = ContainerRuntimeFactory::createRuntime();
    }

    std::unique_ptr<ContainerRuntime> runtime_;
};

TEST_F(RuntimeUtilsTest, IsValidContainerConfig)
{
    ContainerConfig valid_config;
    valid_config.name = "test-container";
    valid_config.image = "ubuntu:latest";
    valid_config.command = {"/bin/echo", "test"};

    EXPECT_TRUE(runtime_utils::isValidContainerConfig(valid_config));

    ContainerConfig invalid_config; // Missing required fields
    EXPECT_FALSE(runtime_utils::isValidContainerConfig(invalid_config));
}

TEST_F(RuntimeUtilsTest, CreateDefaultConfig)
{
    auto config = runtime_utils::createDefaultConfig("nginx:latest");

    EXPECT_EQ(config.image, "nginx:latest");
    EXPECT_GT(config.resources.cpu_shares, 0);
    EXPECT_FALSE(config.name.empty());
}

TEST_F(RuntimeUtilsTest, MergeConfigs)
{
    ContainerConfig base;
    base.name = "base-container";
    base.image = "ubuntu:latest";
    base.command = {"/bin/echo", "base"};
    base.env = {"BASE_VAR=base_value"};

    ContainerConfig override;
    override.name = "override-container";
    override.command = {"/bin/echo", "override"};
    override.env = {"OVERRIDE_VAR=override_value"};

    auto merged = runtime_utils::mergeConfigs(base, override);

    EXPECT_EQ(merged.name, "override-container"); // Override takes precedence
    EXPECT_EQ(merged.image, "ubuntu:latest");     // Base value preserved
    EXPECT_EQ(merged.command, override.command);  // Override takes precedence
    EXPECT_EQ(merged.env.size(), 2);              // Both env vars should be present
}

TEST_F(RuntimeUtilsTest, SanitizeContainerName)
{
    std::string sanitized = runtime_utils::sanitizeContainerName("Test@Container#123!");
    EXPECT_EQ(sanitized, "Test-Container-123");

    sanitized = runtime_utils::sanitizeContainerName("   spaced   name   ");
    EXPECT_EQ(sanitized, "spaced-name");
}

TEST_F(RuntimeUtilsTest, CalculateMemoryLimit)
{
    size_t limit1 = runtime_utils::calculateMemoryLimit("512m");
    EXPECT_EQ(limit1, 512 * 1024 * 1024);

    size_t limit2 = runtime_utils::calculateMemoryLimit("1g");
    EXPECT_EQ(limit2, 1024 * 1024 * 1024);

    size_t limit3 = runtime_utils::calculateMemoryLimit("1073741824");
    EXPECT_EQ(limit3, 1024 * 1024 * 1024);
}

TEST_F(RuntimeUtilsTest, CalculateCpuShares)
{
    double shares1 = runtime_utils::calculateCpuShares("2");
    EXPECT_EQ(shares1, 2.0);

    double shares2 = runtime_utils::calculateCpuShares("1.5");
    EXPECT_EQ(shares2, 1.5);
}

TEST_F(RuntimeUtilsTest, MeasureOperation)
{
    auto duration = runtime_utils::measureOperation(
        []() { std::this_thread::sleep_for(std::chrono::milliseconds(100)); });

    EXPECT_GE(duration.count(), 90);  // At least 90ms
    EXPECT_LE(duration.count(), 200); // But not more than 200ms
}
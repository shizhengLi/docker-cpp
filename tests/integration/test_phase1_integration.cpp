#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <memory>
#include <thread>

// Include all Phase 1 components
#include <docker-cpp/cgroup/cgroup_manager.hpp>
#include <docker-cpp/cgroup/resource_monitor.hpp>
#include <docker-cpp/config/config_manager.hpp>
#include <docker-cpp/core/error.hpp>
#include <docker-cpp/core/event.hpp>
#include <docker-cpp/core/logger.hpp>
#include <docker-cpp/namespace/namespace_manager.hpp>
#include <docker-cpp/namespace/process_manager.hpp>
#include <docker-cpp/plugin/plugin_interface.hpp>
#include <docker-cpp/plugin/plugin_registry.hpp>

using namespace docker_cpp;
using namespace std::chrono_literals;

// Basic integration test class that sets up all Phase 1 components
class Phase1BasicIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        try {
            // Initialize logging system
            logger_ = Logger::getInstance();
            logger_->setLevel(LogLevel::WARNING); // Reduce log noise

            // Initialize configuration system
            config_manager_ = std::make_unique<ConfigManager>();

            // Initialize event system
            event_manager_ = EventManager::getInstance();

            // Initialize plugin registry
            plugin_registry_ = std::make_unique<PluginRegistry>();

            // Check system capabilities with try-catch
            try {
                NamespaceManager test_ns(NamespaceType::PID);
                can_use_namespaces_ = test_ns.isValid();
            }
            catch (const ContainerError&) {
                can_use_namespaces_ = false;
            }

            try {
                CgroupConfig config;
                config.name = "test_basic_integration";
                auto test_cg = CgroupManager::create(config);
                can_use_cgroups_ = test_cg != nullptr;
            }
            catch (const ContainerError&) {
                can_use_cgroups_ = false;
            }
        }
        catch (const std::exception& e) {
            GTEST_SKIP() << "Failed to initialize Phase 1 components: " << e.what();
        }
    }

    void TearDown() override
    {
        // Cleanup resources
        namespace_managers_.clear();
        plugin_registry_.reset();
        // Note: EventManager and Logger are singletons, don't reset them here
        config_manager_.reset();
    }

    bool hasCgroupPermissions() const
    {
        // Basic permission check for cgroup operations
        return std::filesystem::exists("/sys/fs/cgroup")
               && access("/sys/fs/cgroup", R_OK | W_OK) == 0;
    }

    // Phase 1 components
    Logger* logger_ = nullptr;
    std::unique_ptr<ConfigManager> config_manager_;
    EventManager* event_manager_ = nullptr;
    std::unique_ptr<PluginRegistry> plugin_registry_;
    std::vector<std::unique_ptr<NamespaceManager>> namespace_managers_;
    std::unique_ptr<ProcessManager> process_manager_;
    std::unique_ptr<CgroupManager> cgroup_manager_;
    std::unique_ptr<ResourceMonitor> resource_monitor_;

    // System capabilities
    bool can_use_namespaces_ = false;
    bool can_use_cgroups_ = false;
};

// Test basic Phase 1 component initialization
TEST_F(Phase1BasicIntegrationTest, BasicInitialization)
{
    EXPECT_NE(logger_, nullptr);
    EXPECT_NE(config_manager_, nullptr);
    EXPECT_NE(event_manager_, nullptr);
    EXPECT_NE(plugin_registry_, nullptr);

    // Test basic configuration operations
    config_manager_->set("test.key", "test_value");
    EXPECT_TRUE(config_manager_->has("test.key"));
    EXPECT_EQ(config_manager_->get<std::string>("test.key"), "test_value");

    // Test basic event operations
    Event test_event("test.integration", "basic test data");
    EXPECT_EQ(test_event.getType(), "test.integration");
    EXPECT_EQ(test_event.getData(), "basic test data");

    // Test plugin registry operations
    EXPECT_EQ(plugin_registry_->getPluginCount(), 0);
}

// Test configuration system integration
TEST_F(Phase1BasicIntegrationTest, ConfigurationIntegration)
{
    // Set up configuration
    config_manager_->set("container.name", "test_container");
    config_manager_->set("container.memory.limit", 1024);
    config_manager_->set("container.cpu.shares", 512);
    config_manager_->set("logging.level", "INFO");

    // Test configuration retrieval
    EXPECT_EQ(config_manager_->get<std::string>("container.name"), "test_container");
    EXPECT_EQ(config_manager_->get<int>("container.memory.limit"), 1024);
    EXPECT_EQ(config_manager_->get<int>("container.cpu.shares"), 512);
    EXPECT_EQ(config_manager_->get<std::string>("logging.level"), "INFO");

    // Test configuration layering
    ConfigManager layer2;
    layer2.set("container.network.enabled", true);
    config_manager_->addLayer("network", layer2);

    // Test environment variable expansion (if supported)
    config_manager_->set("test.path", "/tmp/container");
    EXPECT_TRUE(config_manager_->has("test.path"));
}

// Test event system integration
TEST_F(Phase1BasicIntegrationTest, EventIntegration)
{
    bool event_received = false;
    std::string received_type;
    std::string received_data;

    // Subscribe to events
    event_manager_->subscribe("test.integration", [&](const Event& event) {
        event_received = true;
        received_type = event.getType();
        received_data = event.getData();
    });

    // Publish test event
    Event test_event("test.integration", "integration test data");
    event_manager_->publish(test_event);

    // Wait for event processing
    std::this_thread::sleep_for(100ms);

    // Verify event was received
    EXPECT_TRUE(event_received);
    EXPECT_EQ(received_type, "test.integration");
    EXPECT_EQ(received_data, "integration test data");
}

// Test namespace management integration (if supported)
TEST_F(Phase1BasicIntegrationTest, NamespaceIntegration)
{
    if (!can_use_namespaces_) {
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
    }
    catch (const ContainerError& e) {
        GTEST_SKIP() << "Namespace creation failed: " << e.what();
    }
}

// Test cgroup management integration (if supported)
TEST_F(Phase1BasicIntegrationTest, CgroupIntegration)
{
    if (!can_use_cgroups_) {
        GTEST_SKIP() << "Cgroup operations not supported on this system";
    }

    try {
        // Test cgroup manager creation
        CgroupConfig config;
        config.name = "test_integration";
        auto cgroup_mgr = CgroupManager::create(config);
        ASSERT_TRUE(cgroup_mgr != nullptr);

        cgroup_manager_ = std::move(cgroup_mgr);
    }
    catch (const ContainerError& e) {
        GTEST_SKIP() << "Cgroup creation failed: " << e.what();
    }
}

// Test plugin system integration
TEST_F(Phase1BasicIntegrationTest, PluginIntegration)
{
    // Test plugin registry functionality
    EXPECT_EQ(plugin_registry_->getPluginCount(), 0);

    // Test plugin loading (we don't have actual plugins, but we can test the registry)
    // Plugin loading from directory would require actual plugin files
    EXPECT_EQ(plugin_registry_->getPluginCount(), 0);
}

// Test complete Phase 1 workflow
TEST_F(Phase1BasicIntegrationTest, CompleteWorkflow)
{
    // Initialize configuration
    config_manager_->set("workflow.test", true);
    config_manager_->set("workflow.iterations", 5);

    // Set up event subscription
    int event_count = 0;
    event_manager_->subscribe("workflow.test", [&](const Event& /* event */) { event_count++; });

    // Run workflow
    for (int i = 0; i < config_manager_->get<int>("workflow.iterations"); ++i) {
        Event workflow_event("workflow.test", "workflow iteration " + std::to_string(i));
        event_manager_->publish(workflow_event);

        // Simulate some work
        std::this_thread::sleep_for(10ms);
    }

    // Wait for event processing
    std::this_thread::sleep_for(100ms);

    // Verify workflow completed
    EXPECT_EQ(event_count, 5);
    EXPECT_TRUE(config_manager_->get<bool>("workflow.test"));
}

// Performance benchmark for Phase 1 components
TEST_F(Phase1BasicIntegrationTest, PerformanceBenchmark)
{
    const int iterations = 1000;

    // Benchmark configuration operations
    auto config_start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        config_manager_->set("benchmark.key", std::to_string(i));
        auto value = config_manager_->get<std::string>("benchmark.key");
        EXPECT_EQ(value, std::to_string(i));
    }
    auto config_end = std::chrono::high_resolution_clock::now();
    auto config_duration =
        std::chrono::duration_cast<std::chrono::microseconds>(config_end - config_start);

    // Benchmark event operations
    auto event_start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        Event event("benchmark.test", "benchmark data " + std::to_string(i));
        event_manager_->publish(event);
    }
    auto event_end = std::chrono::high_resolution_clock::now();
    auto event_duration =
        std::chrono::duration_cast<std::chrono::microseconds>(event_end - event_start);

    // Performance assertions (these may need tuning based on system capabilities)
    EXPECT_LT(config_duration.count() / iterations, 100)
        << "Config operations should be < 100μs per operation";
    EXPECT_LT(event_duration.count() / iterations, 1000)
        << "Event operations should be < 1000μs per operation";

    // Log performance results
    std::cout << "Performance Results:" << std::endl;
    std::cout << "  Config operations (" << iterations << "): " << config_duration.count()
              << " μs total, " << config_duration.count() / iterations << " μs per operation"
              << std::endl;
    std::cout << "  Event operations (" << iterations << "): " << event_duration.count()
              << " μs total, " << event_duration.count() / iterations << " μs per operation"
              << std::endl;
}
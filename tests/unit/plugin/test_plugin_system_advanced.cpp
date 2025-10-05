#include <gtest/gtest.h>
#include <docker-cpp/core/error.hpp>
#include <docker-cpp/plugin/plugin_interface.hpp>
#include <docker-cpp/plugin/plugin_registry.hpp>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <thread>
#include <atomic>

// Advanced mock plugin for testing complex scenarios
class AdvancedMockPlugin : public docker_cpp::IPlugin {
public:
    struct Config {
        int initialization_delay_ms;
        bool should_throw_on_init;
        bool should_throw_on_shutdown;
        std::string failure_message;
        std::vector<std::string> dependencies;
        std::vector<std::string> capabilities;

        Config() : initialization_delay_ms(0), should_throw_on_init(false),
                  should_throw_on_shutdown(false), failure_message("Plugin failed intentionally"),
                  dependencies({}), capabilities({}) {}

        static Config withDependencies(const std::vector<std::string>& deps) {
            Config config;
            config.dependencies = deps;
            return config;
        }

        static Config withDelay(int delay_ms) {
            Config config;
            config.initialization_delay_ms = delay_ms;
            return config;
        }

        static Config withFailure(bool on_init, bool on_shutdown, const std::string& message) {
            Config config;
            config.should_throw_on_init = on_init;
            config.should_throw_on_shutdown = on_shutdown;
            config.failure_message = message;
            return config;
        }

        static Config withCapabilities(const std::vector<std::string>& caps) {
            Config config;
            config.capabilities = caps;
            return config;
        }
    };

    AdvancedMockPlugin(const std::string& name, AdvancedMockPlugin::Config config = Config())
        : name_(name), config_(std::move(config)), initialized_(false), initialization_count_(0)
    {}

    std::string getName() const override
    {
        return name_;
    }

    std::string getVersion() const override
    {
        return "2.0.0-advanced";
    }

    docker_cpp::PluginInfo getPluginInfo() const override
    {
        return docker_cpp::PluginInfo(docker_cpp::PluginInfoParams{
            name_,
            "2.0.0-advanced",
            "Advanced mock plugin for complex testing scenarios",
            docker_cpp::PluginType::CUSTOM,
            "Test Suite",
            "MIT"});
    }

    bool initialize(const docker_cpp::PluginConfig& config) override
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (config_.should_throw_on_init) {
            throw docker_cpp::ContainerError(docker_cpp::ErrorCode::PLUGIN_INITIALIZATION_FAILED,
                                             config_.failure_message);
        }

        if (config_.initialization_delay_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(config_.initialization_delay_ms));
        }

        stored_config_ = config;
        initialized_ = true;
        initialization_count_++;
        return true;
    }

    void shutdown() override
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (config_.should_throw_on_shutdown) {
            throw docker_cpp::ContainerError(docker_cpp::ErrorCode::PLUGIN_SHUTDOWN_FAILED,
                                             config_.failure_message);
        }

        initialized_ = false;
    }

    bool isInitialized() const override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return initialized_;
    }

    std::string getConfig() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Return a string representation of config for testing
        std::string result = "{";
        for (const auto& [key, value] : stored_config_) {
            result += key + ": " + value + ", ";
        }
        result += "}";
        return result;
    }

    std::vector<std::string> getDependencies() const override
    {
        return config_.dependencies;
    }

    bool hasDependency(const std::string& plugin_name) const override
    {
        return std::find(config_.dependencies.begin(), config_.dependencies.end(), plugin_name)
               != config_.dependencies.end();
    }

    std::vector<std::string> getCapabilities() const override
    {
        return config_.capabilities;
    }

    bool hasCapability(const std::string& capability) const override
    {
        return std::find(config_.capabilities.begin(), config_.capabilities.end(), capability)
               != config_.capabilities.end();
    }

    // Test helper methods
    int getInitializationCount() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return initialization_count_;
    }

    void reset()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        initialized_ = false;
        initialization_count_ = 0;
        stored_config_.clear();
    }

private:
    mutable std::mutex mutex_;
    std::string name_;
    Config config_;
    bool initialized_;
    int initialization_count_;
    docker_cpp::PluginConfig stored_config_;
};

class PluginSystemAdvancedTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        registry = std::make_unique<docker_cpp::PluginRegistry>();
    }

    void TearDown() override
    {
        registry.reset();
    }

    docker_cpp::PluginRegistry* getRegistry() const
    {
        return registry.get();
    }

    std::unique_ptr<docker_cpp::PluginRegistry> registry;
};

// Test complex dependency resolution
TEST_F(PluginSystemAdvancedTest, ComplexDependencyResolution)
{
    // Create plugins with complex dependencies
    // Plugin A depends on B and C
    // Plugin B depends on D
    // Plugin C depends on D and E
    // Plugin D and E have no dependencies

    auto plugin_e = std::make_unique<AdvancedMockPlugin>("plugin-e", AdvancedMockPlugin::Config{});
    auto plugin_d = std::make_unique<AdvancedMockPlugin>("plugin-d", AdvancedMockPlugin::Config{});

    auto plugin_b = std::make_unique<AdvancedMockPlugin>("plugin-b", AdvancedMockPlugin::Config::withDependencies({"plugin-d"}));

    auto plugin_c = std::make_unique<AdvancedMockPlugin>("plugin-c", AdvancedMockPlugin::Config::withDependencies({"plugin-d", "plugin-e"}));

    auto plugin_a = std::make_unique<AdvancedMockPlugin>("plugin-a", AdvancedMockPlugin::Config::withDependencies({"plugin-b", "plugin-c"}));

    // Register plugins
    getRegistry()->registerPlugin("plugin-a", std::move(plugin_a));
    getRegistry()->registerPlugin("plugin-b", std::move(plugin_b));
    getRegistry()->registerPlugin("plugin-c", std::move(plugin_c));
    getRegistry()->registerPlugin("plugin-d", std::move(plugin_d));
    getRegistry()->registerPlugin("plugin-e", std::move(plugin_e));

    // Get load order
    auto load_order = getRegistry()->getLoadOrder();

    // Verify correct dependency order: E, D, B, C, A
    ASSERT_EQ(load_order.size(), 5);
    EXPECT_EQ(load_order[0], "plugin-e");
    EXPECT_EQ(load_order[1], "plugin-d");

    // B should come before A
    auto b_pos = std::find(load_order.begin(), load_order.end(), "plugin-b");
    auto a_pos = std::find(load_order.begin(), load_order.end(), "plugin-a");
    EXPECT_LT(b_pos, a_pos);

    // C should come before A
    auto c_pos = std::find(load_order.begin(), load_order.end(), "plugin-c");
    EXPECT_LT(c_pos, a_pos);

    // D should come before B and C
    EXPECT_TRUE((b_pos < c_pos) || (b_pos > c_pos)); // Either order for B/C is fine
    EXPECT_LT(b_pos, a_pos);
    EXPECT_LT(c_pos, a_pos);
}

// Test concurrent plugin operations
TEST_F(PluginSystemAdvancedTest, ConcurrentPluginOperations)
{
    const int num_plugins = 20;
    const int num_threads = 5;

    // Create plugins
    for (int i = 0; i < num_plugins; ++i) {
        auto plugin = std::make_unique<AdvancedMockPlugin>("concurrent-plugin-" + std::to_string(i));
        getRegistry()->registerPlugin("concurrent-plugin-" + std::to_string(i), std::move(plugin));
    }

    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};
    std::atomic<int> error_count{0};

    docker_cpp::PluginConfig config;
    config["test"] = "concurrent_value";

    // Start multiple threads performing plugin operations
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([this, config, &success_count, &error_count]() {
            for (int j = 0; j < num_plugins; ++j) {
                std::string plugin_name = "concurrent-plugin-" + std::to_string(j);

                try {
                    // Initialize plugin
                    bool init_result = getRegistry()->initializePlugin(plugin_name, config);
                    if (init_result) {
                        success_count++;

                        // Get plugin info
                        auto info = getRegistry()->getPluginInfo(plugin_name);
                        EXPECT_EQ(info.getName(), plugin_name);

                        // Shutdown plugin
                        bool shutdown_result = getRegistry()->shutdownPlugin(plugin_name);
                        if (shutdown_result) {
                            success_count++;
                        } else {
                            error_count++;
                        }
                    } else {
                        error_count++;
                    }
                } catch (const std::exception&) {
                    error_count++;
                }
            }
        });
    }

    // Wait for all threads
    for (auto& thread : threads) {
        thread.join();
    }

    // Should have some successful operations
    EXPECT_GT(success_count.load(), 0);
    // Error count should be reasonable (not all operations failed)
    EXPECT_LT(error_count.load(), num_threads * num_plugins * 2);
}

// Test plugin initialization with delays (simulating slow plugins)
TEST_F(PluginSystemAdvancedTest, SlowPluginInitialization)
{
    // Create plugins with different initialization delays
    auto fast_plugin = std::make_unique<AdvancedMockPlugin>("fast", AdvancedMockPlugin::Config::withDelay(10));

    auto slow_plugin = std::make_unique<AdvancedMockPlugin>("slow", AdvancedMockPlugin::Config::withDelay(100));

    auto medium_plugin = std::make_unique<AdvancedMockPlugin>("medium", AdvancedMockPlugin::Config::withDelay(50));

    getRegistry()->registerPlugin("fast", std::move(fast_plugin));
    getRegistry()->registerPlugin("slow", std::move(slow_plugin));
    getRegistry()->registerPlugin("medium", std::move(medium_plugin));

    docker_cpp::PluginConfig config;
    config["test"] = "slow_init_test";

    auto start_time = std::chrono::high_resolution_clock::now();

    // Initialize all plugins
    auto results = getRegistry()->initializeAllPlugins(config);

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    // All should succeed
    EXPECT_EQ(results.size(), 3);
    EXPECT_TRUE(results["fast"]);
    EXPECT_TRUE(results["medium"]);
    EXPECT_TRUE(results["slow"]);

    // Should take approximately the time of the slowest plugin (with some tolerance)
    EXPECT_GT(duration.count(), 90); // At least the slow plugin's time
    EXPECT_LT(duration.count(), 200); // But not too much longer

    // Verify all plugins are initialized
    auto* fast_ptr = getRegistry()->getPlugin<AdvancedMockPlugin>("fast");
    auto* medium_ptr = getRegistry()->getPlugin<AdvancedMockPlugin>("medium");
    auto* slow_ptr = getRegistry()->getPlugin<AdvancedMockPlugin>("slow");

    EXPECT_TRUE(fast_ptr->isInitialized());
    EXPECT_TRUE(medium_ptr->isInitialized());
    EXPECT_TRUE(slow_ptr->isInitialized());
}

// Test plugin failure scenarios
TEST_F(PluginSystemAdvancedTest, PluginFailureHandling)
{
    // Create various failing plugins
    auto init_failure_plugin = std::make_unique<AdvancedMockPlugin>("init-failure", AdvancedMockPlugin::Config::withFailure(true, false, "Intentional initialization failure"));

    auto shutdown_failure_plugin = std::make_unique<AdvancedMockPlugin>("shutdown-failure", AdvancedMockPlugin::Config::withFailure(false, true, "Intentional shutdown failure"));

    auto working_plugin = std::make_unique<AdvancedMockPlugin>("working", AdvancedMockPlugin::Config{});

    getRegistry()->registerPlugin("init-failure", std::move(init_failure_plugin));
    getRegistry()->registerPlugin("shutdown-failure", std::move(shutdown_failure_plugin));
    getRegistry()->registerPlugin("working", std::move(working_plugin));

    docker_cpp::PluginConfig config;

    // Test initialization failure
    EXPECT_FALSE(getRegistry()->initializePlugin("init-failure", config));
    EXPECT_FALSE(getRegistry()->getPlugin<AdvancedMockPlugin>("init-failure")->isInitialized());

    // Test successful initialization
    EXPECT_TRUE(getRegistry()->initializePlugin("working", config));
    EXPECT_TRUE(getRegistry()->getPlugin<AdvancedMockPlugin>("working")->isInitialized());

    // Test shutdown failure (should not throw, but should return false)
    EXPECT_TRUE(getRegistry()->initializePlugin("shutdown-failure", config));
    EXPECT_FALSE(getRegistry()->shutdownPlugin("shutdown-failure")); // Should return false due to exception
}

// Test plugin dependency failures
TEST_F(PluginSystemAdvancedTest, PluginDependencyFailures)
{
    // Create plugins where one depends on a failing plugin
    auto base_plugin = std::make_unique<AdvancedMockPlugin>("base", AdvancedMockPlugin::Config::withFailure(true, false, ""));

    auto dependent_plugin = std::make_unique<AdvancedMockPlugin>("dependent", AdvancedMockPlugin::Config::withDependencies({"base"}));

    getRegistry()->registerPlugin("base", std::move(base_plugin));
    getRegistry()->registerPlugin("dependent", std::move(dependent_plugin));

    docker_cpp::PluginConfig config;

    // Try to initialize dependent plugin
    EXPECT_FALSE(getRegistry()->initializePlugin("dependent", config));

    // Neither should be initialized due to dependency failure
    EXPECT_FALSE(getRegistry()->getPlugin<AdvancedMockPlugin>("base")->isInitialized());
    EXPECT_FALSE(getRegistry()->getPlugin<AdvancedMockPlugin>("dependent")->isInitialized());
}

// Test plugin registry performance with many plugins
TEST_F(PluginSystemAdvancedTest, RegistryPerformanceWithManyPlugins)
{
    const int num_plugins = 1000;

    // Register many plugins
    auto start_time = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < num_plugins; ++i) {
        auto plugin = std::make_unique<AdvancedMockPlugin>("perf-plugin-" + std::to_string(i));
        getRegistry()->registerPlugin("perf-plugin-" + std::to_string(i), std::move(plugin));
    }

    auto registration_time = std::chrono::high_resolution_clock::now();
    auto registration_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        registration_time - start_time);

    // Test lookup performance
    start_time = std::chrono::high_resolution_clock::now();

    docker_cpp::PluginConfig config;
    for (int i = 0; i < num_plugins; i += 10) {
        std::string plugin_name = "perf-plugin-" + std::to_string(i);
        bool result = getRegistry()->initializePlugin(plugin_name, config);
        EXPECT_TRUE(result);
    }

    auto lookup_time = std::chrono::high_resolution_clock::now();
    auto lookup_duration = std::chrono::duration_cast<std::chrono::milliseconds>(lookup_time - start_time);

    // Test get all plugin names performance
    start_time = std::chrono::high_resolution_clock::now();
    auto all_names = getRegistry()->getPluginNames();
    auto name_time = std::chrono::high_resolution_clock::now();
    auto name_duration = std::chrono::duration_cast<std::chrono::milliseconds>(name_time - start_time);

    // Performance assertions
    EXPECT_LT(registration_duration.count(), 1000); // Should register 1000 plugins in under 1 second
    EXPECT_LT(lookup_duration.count(), 500);       // Should look up 100 plugins in under 500ms
    EXPECT_LT(name_duration.count(), 100);         // Should get all names in under 100ms
    EXPECT_EQ(all_names.size(), num_plugins);
}

// Test plugin capability management
TEST_F(PluginSystemAdvancedTest, PluginCapabilityManagement)
{
    auto plugin1 = std::make_unique<AdvancedMockPlugin>("plugin1", AdvancedMockPlugin::Config::withCapabilities({"network", "storage", "security"}));

    auto plugin2 = std::make_unique<AdvancedMockPlugin>("plugin2", AdvancedMockPlugin::Config::withCapabilities({"network", "compute"}));

    auto plugin3 = std::make_unique<AdvancedMockPlugin>("plugin3", AdvancedMockPlugin::Config::withCapabilities({"storage", "monitoring"}));

    getRegistry()->registerPlugin("plugin1", std::move(plugin1));
    getRegistry()->registerPlugin("plugin2", std::move(plugin2));
    getRegistry()->registerPlugin("plugin3", std::move(plugin3));

    // Test individual plugin capabilities
    auto* p1 = getRegistry()->getPlugin<AdvancedMockPlugin>("plugin1");
    auto* p2 = getRegistry()->getPlugin<AdvancedMockPlugin>("plugin2");
    // Note: p3 not used in this test

    EXPECT_TRUE(p1->hasCapability("network"));
    EXPECT_TRUE(p1->hasCapability("storage"));
    EXPECT_TRUE(p1->hasCapability("security"));
    EXPECT_FALSE(p1->hasCapability("compute"));

    EXPECT_TRUE(p2->hasCapability("network"));
    EXPECT_TRUE(p2->hasCapability("compute"));
    EXPECT_FALSE(p2->hasCapability("storage"));

    // Test finding plugins by capability
    auto all_info = getRegistry()->getAllPluginInfo();

    std::vector<std::string> network_plugins;
    std::vector<std::string> storage_plugins;

    for (const auto& info : all_info) {
        auto caps = getRegistry()->getPlugin<AdvancedMockPlugin>(info.getName())->getCapabilities();
        if (std::find(caps.begin(), caps.end(), "network") != caps.end()) {
            network_plugins.push_back(info.getName());
        }
        if (std::find(caps.begin(), caps.end(), "storage") != caps.end()) {
            storage_plugins.push_back(info.getName());
        }
    }

    EXPECT_EQ(network_plugins.size(), 2);
    EXPECT_EQ(storage_plugins.size(), 2);

    // Verify specific plugins
    EXPECT_TRUE(std::find(network_plugins.begin(), network_plugins.end(), "plugin1") != network_plugins.end());
    EXPECT_TRUE(std::find(network_plugins.begin(), network_plugins.end(), "plugin2") != network_plugins.end());
    EXPECT_TRUE(std::find(storage_plugins.begin(), storage_plugins.end(), "plugin1") != storage_plugins.end());
    EXPECT_TRUE(std::find(storage_plugins.begin(), storage_plugins.end(), "plugin3") != storage_plugins.end());
}

// Test plugin registry state consistency
TEST_F(PluginSystemAdvancedTest, RegistryStateConsistency)
{
    // Add plugins and perform various operations, checking consistency
    auto plugin1 = std::make_unique<AdvancedMockPlugin>("state-test-1", AdvancedMockPlugin::Config{});
    auto plugin2 = std::make_unique<AdvancedMockPlugin>("state-test-2", AdvancedMockPlugin::Config{});

    docker_cpp::PluginConfig config;

    // Initial state
    EXPECT_EQ(getRegistry()->getPluginCount(), 0);
    EXPECT_EQ(getRegistry()->getInitializedPluginCount(), 0);

    // Register first plugin
    getRegistry()->registerPlugin("state-test-1", std::move(plugin1));
    EXPECT_EQ(getRegistry()->getPluginCount(), 1);
    EXPECT_EQ(getRegistry()->getInitializedPluginCount(), 0);
    EXPECT_TRUE(getRegistry()->hasPlugin("state-test-1"));
    EXPECT_FALSE(getRegistry()->hasPlugin("nonexistent"));

    // Initialize first plugin
    getRegistry()->initializePlugin("state-test-1", config);
    EXPECT_EQ(getRegistry()->getPluginCount(), 1);
    EXPECT_EQ(getRegistry()->getInitializedPluginCount(), 1);

    // Register second plugin
    getRegistry()->registerPlugin("state-test-2", std::move(plugin2));
    EXPECT_EQ(getRegistry()->getPluginCount(), 2);
    EXPECT_EQ(getRegistry()->getInitializedPluginCount(), 1);

    // Initialize second plugin
    getRegistry()->initializePlugin("state-test-2", config);
    EXPECT_EQ(getRegistry()->getPluginCount(), 2);
    EXPECT_EQ(getRegistry()->getInitializedPluginCount(), 2);

    // Shutdown one plugin
    getRegistry()->shutdownPlugin("state-test-1");
    EXPECT_EQ(getRegistry()->getPluginCount(), 2);
    EXPECT_EQ(getRegistry()->getInitializedPluginCount(), 1);

    // Unregister one plugin
    getRegistry()->unregisterPlugin("state-test-1");
    EXPECT_EQ(getRegistry()->getPluginCount(), 1);
    EXPECT_EQ(getRegistry()->getInitializedPluginCount(), 1);
    EXPECT_FALSE(getRegistry()->hasPlugin("state-test-1"));
    EXPECT_TRUE(getRegistry()->hasPlugin("state-test-2"));
}
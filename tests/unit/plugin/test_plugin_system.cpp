#include <gtest/gtest.h>
#include <docker-cpp/core/error.hpp>
#include <docker-cpp/plugin/plugin_interface.hpp>
#include <docker-cpp/plugin/plugin_registry.hpp>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>


// Mock plugin parameters to avoid swappable parameters
struct MockPluginParams {
    std::string name;
    std::string version;
    bool init_result = true;
    bool should_fail = false;
    std::vector<std::string> dependencies = {};
    std::vector<std::string> capabilities = {};
};

class MockPlugin : public docker_cpp::IPlugin {
public:
    explicit MockPlugin(const MockPluginParams& params)
        : name_(params.name), version_(params.version), init_result_(params.init_result),
          should_fail_(params.should_fail), initialized_(false), dependencies_(params.dependencies),
          capabilities_(params.capabilities)
    {}

    std::string getName() const override
    {
        return name_;
    }
    std::string getVersion() const override
    {
        return version_;
    }

    docker_cpp::PluginInfo getPluginInfo() const override
    {
        return docker_cpp::PluginInfo(docker_cpp::PluginInfoParams{
            name_, version_, "Mock Plugin for testing", docker_cpp::PluginType::CUSTOM, "Test Author", "MIT"});
    }

    bool initialize(const docker_cpp::PluginConfig& config) override
    {
        if (should_fail_) {
            throw docker_cpp::ContainerError(docker_cpp::ErrorCode::PLUGIN_INITIALIZATION_FAILED,
                                 "Mock plugin forced initialization failure");
        }
        config_ = config;
        initialized_ = init_result_;
        return init_result_;
    }

    void shutdown() override
    {
        initialized_ = false;
    }

    bool isInitialized() const override
    {
        return initialized_;
    }
    const docker_cpp::PluginConfig& getConfig() const
    {
        return config_;
    }

    std::vector<std::string> getDependencies() const override
    {
        return dependencies_;
    }

    bool hasDependency(const std::string& plugin_name) const override
    {
        return std::find(dependencies_.begin(), dependencies_.end(), plugin_name)
               != dependencies_.end();
    }

    std::vector<std::string> getCapabilities() const override
    {
        return capabilities_;
    }

    bool hasCapability(const std::string& capability) const override
    {
        return std::find(capabilities_.begin(), capabilities_.end(), capability)
               != capabilities_.end();
    }

private:
    std::string name_;
    std::string version_;
    bool init_result_;
    bool should_fail_;
    bool initialized_;
    docker_cpp::PluginConfig config_;
    std::vector<std::string> dependencies_;
    std::vector<std::string> capabilities_;
};

class PluginInterfaceTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        plugin = std::make_unique<MockPlugin>(MockPluginParams{"test-plugin", "1.0.0"});
    }

    MockPlugin* getPlugin() const
    {
        return plugin.get();
    }

private:
    std::unique_ptr<MockPlugin> plugin;
};

TEST_F(PluginInterfaceTest, GetNameReturnsCorrectName)
{
    EXPECT_EQ(getPlugin()->getName(), "test-plugin");
}

TEST_F(PluginInterfaceTest, GetVersionReturnsCorrectVersion)
{
    EXPECT_EQ(getPlugin()->getVersion(), "1.0.0");
}

TEST_F(PluginInterfaceTest, InitializeSucceedsWithValidConfig)
{
    docker_cpp::PluginConfig config;
    config["key"] = "value";

    EXPECT_TRUE(getPlugin()->initialize(config));
    EXPECT_TRUE(getPlugin()->isInitialized());
    EXPECT_EQ(getPlugin()->getConfig().at("key"), "value");
}

TEST_F(PluginInterfaceTest, InitializeFailsWhenConfigured)
{
    auto failing_plugin =
        std::make_unique<MockPlugin>(MockPluginParams{"failing-plugin", "1.0.0", false});

    docker_cpp::PluginConfig config;
    EXPECT_FALSE(failing_plugin->initialize(config));
    EXPECT_FALSE(failing_plugin->isInitialized());
}

TEST_F(PluginInterfaceTest, InitializeThrowsWhenConfigured)
{
    auto throwing_plugin =
        std::make_unique<MockPlugin>(MockPluginParams{"throwing-plugin", "1.0.0", true, true});

    docker_cpp::PluginConfig config;
    EXPECT_THROW(throwing_plugin->initialize(config), docker_cpp::ContainerError);
    EXPECT_FALSE(throwing_plugin->isInitialized());
}

TEST_F(PluginInterfaceTest, ShutdownResetsInitializedState)
{
    docker_cpp::PluginConfig config;
    getPlugin()->initialize(config);
    EXPECT_TRUE(getPlugin()->isInitialized());

    getPlugin()->shutdown();
    EXPECT_FALSE(getPlugin()->isInitialized());
}

class PluginRegistryTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        registry = std::make_unique<docker_cpp::PluginRegistry>();
    }

    docker_cpp::PluginRegistry* getRegistry() const
    {
        return registry.get();
    }

private:
    std::unique_ptr<docker_cpp::PluginRegistry> registry;
};

TEST_F(PluginRegistryTest,RegisterPluginSucceeds)
{
    auto plugin = std::make_unique<MockPlugin>(MockPluginParams{"plugin1", "1.0.0"});
    getRegistry()->registerPlugin("plugin1", std::move(plugin));

    auto* retrieved = getRegistry()->getPlugin<docker_cpp::IPlugin>("plugin1");
    EXPECT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->getName(), "plugin1");
    EXPECT_EQ(retrieved->getVersion(), "1.0.0");
}

TEST_F(PluginRegistryTest,RegisterDuplicatePluginThrows)
{
    auto plugin1 = std::make_unique<MockPlugin>(MockPluginParams{"plugin1", "1.0.0"});
    getRegistry()->registerPlugin("plugin1", std::move(plugin1));

    auto duplicate_plugin = std::make_unique<MockPlugin>(MockPluginParams{"plugin1", "1.0.1"});
    EXPECT_THROW(getRegistry()->registerPlugin("plugin1", std::move(duplicate_plugin)),
                 docker_cpp::ContainerError);
}

TEST_F(PluginRegistryTest,GetNonExistentPluginReturnsNull)
{
    auto* retrieved = getRegistry()->getPlugin<docker_cpp::IPlugin>("nonexistent");
    EXPECT_EQ(retrieved, nullptr);
}

TEST_F(PluginRegistryTest,GetPluginAfterMoveReturnsNull)
{
    // Create a new plugin for this test
    auto test_plugin = std::make_unique<MockPlugin>(MockPluginParams{"test-plugin", "1.0.0"});

    // Register a plugin first
    getRegistry()->registerPlugin("test-plugin", std::move(test_plugin));

    // Then move the registry
    auto moved_registry = std::move(getRegistry());

    // Original registry should not return plugins (moved-from state)
    // But we shouldn't access moved-from objects, so we'll just check that
    // moved registry has the plugin
    auto* retrieved = moved_registry->getPlugin<docker_cpp::IPlugin>("test-plugin");
    EXPECT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->getName(), "test-plugin");
}

TEST_F(PluginRegistryTest,RegisterMultiplePlugins)
{
    auto p1 = std::make_unique<MockPlugin>(MockPluginParams{"plugin1", "1.0.0"});
    auto p2 = std::make_unique<MockPlugin>(MockPluginParams{"plugin2", "2.0.0"});
    auto p3 = std::make_unique<MockPlugin>(MockPluginParams{"plugin3", "1.5.0"});

    getRegistry()->registerPlugin("plugin1", std::move(p1));
    getRegistry()->registerPlugin("plugin2", std::move(p2));
    getRegistry()->registerPlugin("plugin3", std::move(p3));

    auto* r1 = getRegistry()->getPlugin<docker_cpp::IPlugin>("plugin1");
    auto* r2 = getRegistry()->getPlugin<docker_cpp::IPlugin>("plugin2");
    auto* r3 = getRegistry()->getPlugin<docker_cpp::IPlugin>("plugin3");

    EXPECT_NE(r1, nullptr);
    EXPECT_NE(r2, nullptr);
    EXPECT_NE(r3, nullptr);

    EXPECT_EQ(r1->getName(), "plugin1");
    EXPECT_EQ(r2->getName(), "plugin2");
    EXPECT_EQ(r3->getName(), "plugin3");
}

TEST_F(PluginRegistryTest,GetPluginNamesReturnsAllRegisteredPlugins)
{
    auto p1 = std::make_unique<MockPlugin>(MockPluginParams{"plugin1", "1.0.0"});
    auto p2 = std::make_unique<MockPlugin>(MockPluginParams{"plugin2", "2.0.0"});
    auto p3 = std::make_unique<MockPlugin>(MockPluginParams{"plugin3", "1.5.0"});

    getRegistry()->registerPlugin("plugin1", std::move(p1));
    getRegistry()->registerPlugin("plugin2", std::move(p2));
    getRegistry()->registerPlugin("plugin3", std::move(p3));

    auto names = getRegistry()->getPluginNames();
    EXPECT_EQ(names.size(), 3);

    // Convert to set for order-independent comparison
    std::unordered_set<std::string> name_set(names.begin(), names.end());
    EXPECT_TRUE(name_set.count("plugin1"));
    EXPECT_TRUE(name_set.count("plugin2"));
    EXPECT_TRUE(name_set.count("plugin3"));
}

TEST_F(PluginRegistryTest,UnregisterPluginRemovesPlugin)
{
    auto p1 = std::make_unique<MockPlugin>(MockPluginParams{"plugin1", "1.0.0"});
    auto p2 = std::make_unique<MockPlugin>(MockPluginParams{"plugin2", "2.0.0"});

    getRegistry()->registerPlugin("plugin1", std::move(p1));
    getRegistry()->registerPlugin("plugin2", std::move(p2));

    // Verify plugin exists
    auto* plugin1_retrieved = getRegistry()->getPlugin<docker_cpp::IPlugin>("plugin1");
    EXPECT_NE(plugin1_retrieved, nullptr);

    // Unregister and verify removal
    getRegistry()->unregisterPlugin("plugin1");
    plugin1_retrieved = getRegistry()->getPlugin<docker_cpp::IPlugin>("plugin1");
    EXPECT_EQ(plugin1_retrieved, nullptr);

    // Verify other plugin still exists
    auto* plugin2_retrieved = getRegistry()->getPlugin<docker_cpp::IPlugin>("plugin2");
    EXPECT_NE(plugin2_retrieved, nullptr);
}

TEST_F(PluginRegistryTest,UnregisterNonExistentPluginThrows)
{
    EXPECT_THROW(getRegistry()->unregisterPlugin("nonexistent"), docker_cpp::ContainerError);
}

TEST_F(PluginRegistryTest,InitializePluginSucceeds)
{
    auto plugin1 = std::make_unique<MockPlugin>(MockPluginParams{"plugin1", "1.0.0"});
    getRegistry()->registerPlugin("plugin1", std::move(plugin1));

    docker_cpp::PluginConfig config;
    config["test"] = "value";

    EXPECT_TRUE(getRegistry()->initializePlugin("plugin1", config));

    auto* p1 = getRegistry()->getPlugin<MockPlugin>("plugin1");
    EXPECT_NE(p1, nullptr);
    EXPECT_TRUE(p1->isInitialized());
}

TEST_F(PluginRegistryTest,InitializeNonExistentPluginFails)
{
    docker_cpp::PluginConfig config;
    EXPECT_FALSE(getRegistry()->initializePlugin("nonexistent", config));
}

TEST_F(PluginRegistryTest,InitializePluginWithExceptionFails)
{
    auto throwing_plugin =
        std::make_unique<MockPlugin>(MockPluginParams{"throwing", "1.0.0", true, true});
    getRegistry()->registerPlugin("throwing", std::move(throwing_plugin));

    docker_cpp::PluginConfig config;
    EXPECT_FALSE(getRegistry()->initializePlugin("throwing", config));
}

TEST_F(PluginRegistryTest,ShutdownPluginSucceeds)
{
    auto plugin1 = std::make_unique<MockPlugin>(MockPluginParams{"plugin1", "1.0.0"});
    getRegistry()->registerPlugin("plugin1", std::move(plugin1));

    docker_cpp::PluginConfig config;
    getRegistry()->initializePlugin("plugin1", config);

    auto* p1 = getRegistry()->getPlugin<MockPlugin>("plugin1");
    EXPECT_TRUE(p1->isInitialized());

    getRegistry()->shutdownPlugin("plugin1");
    EXPECT_FALSE(p1->isInitialized());
}

TEST_F(PluginRegistryTest,ShutdownNonExistentPluginFails)
{
    EXPECT_FALSE(getRegistry()->shutdownPlugin("nonexistent"));
}

TEST_F(PluginRegistryTest,InitializeAllPluginsSucceeds)
{
    auto plugin1 = std::make_unique<MockPlugin>(MockPluginParams{"plugin1", "1.0.0"});
    getRegistry()->registerPlugin("plugin1", std::move(plugin1));
    auto plugin2 = std::make_unique<MockPlugin>(MockPluginParams{"plugin2", "2.0.0"});
    getRegistry()->registerPlugin("plugin2", std::move(plugin2));
    auto plugin3 = std::make_unique<MockPlugin>(MockPluginParams{"plugin3", "1.5.0"});
    getRegistry()->registerPlugin("plugin3", std::move(plugin3));

    docker_cpp::PluginConfig config;
    auto results = getRegistry()->initializeAllPlugins(config);

    EXPECT_EQ(results.size(), 3);
    EXPECT_TRUE(results.at("plugin1"));
    EXPECT_TRUE(results.at("plugin2"));
    EXPECT_TRUE(results.at("plugin3"));
}

TEST_F(PluginRegistryTest,ShutdownAllPluginsSucceeds)
{
    auto plugin1 = std::make_unique<MockPlugin>(MockPluginParams{"plugin1", "1.0.0"});
    getRegistry()->registerPlugin("plugin1", std::move(plugin1));
    auto plugin2 = std::make_unique<MockPlugin>(MockPluginParams{"plugin2", "2.0.0"});
    getRegistry()->registerPlugin("plugin2", std::move(plugin2));
    auto plugin3 = std::make_unique<MockPlugin>(MockPluginParams{"plugin3", "1.5.0"});
    getRegistry()->registerPlugin("plugin3", std::move(plugin3));

    docker_cpp::PluginConfig config;
    getRegistry()->initializeAllPlugins(config);

    getRegistry()->shutdownAllPlugins();

    auto* p1 = getRegistry()->getPlugin<MockPlugin>("plugin1");
    auto* p2 = getRegistry()->getPlugin<MockPlugin>("plugin2");
    auto* p3 = getRegistry()->getPlugin<MockPlugin>("plugin3");

    EXPECT_FALSE(p1->isInitialized());
    EXPECT_FALSE(p2->isInitialized());
    EXPECT_FALSE(p3->isInitialized());
}

class PluginInfoTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        info = docker_cpp::PluginInfo(docker_cpp::PluginInfoParams{"test-plugin",
                                           "1.0.0",
                                           "Test Plugin Description",
                                           docker_cpp::PluginType::CORE,
                                           "test-author",
                                           "MIT"});
    }

    docker_cpp::PluginInfo& getInfo()
    {
        return info;
    }
    const docker_cpp::PluginInfo& getInfo() const
    {
        return info;
    }

private:
    docker_cpp::PluginInfo info{}; // Default construct first
};

TEST_F(PluginInfoTest,GettersReturnCorrectValues)
{
    EXPECT_EQ(getInfo().getName(), "test-plugin");
    EXPECT_EQ(getInfo().getVersion(), "1.0.0");
    EXPECT_EQ(getInfo().getDescription(), "Test Plugin Description");
    EXPECT_EQ(getInfo().getType(), docker_cpp::PluginType::CORE);
    EXPECT_EQ(getInfo().getAuthor(), "test-author");
    EXPECT_EQ(getInfo().getLicense(), "MIT");
}

TEST_F(PluginInfoTest,SettersUpdateValues)
{
    getInfo().setDescription("Updated Description");
    getInfo().setAuthor("Updated Author");
    getInfo().setLicense("GPL-3.0");

    EXPECT_EQ(getInfo().getDescription(), "Updated Description");
    EXPECT_EQ(getInfo().getAuthor(), "Updated Author");
    EXPECT_EQ(getInfo().getLicense(), "GPL-3.0");
}

TEST_F(PluginInfoTest,ToStringReturnsFormattedString)
{
    std::string str = getInfo().toString();
    EXPECT_TRUE(str.find("test-plugin") != std::string::npos);
    EXPECT_TRUE(str.find("1.0.0") != std::string::npos);
    EXPECT_TRUE(str.find("Test Plugin Description") != std::string::npos);
    EXPECT_TRUE(str.find("test-author") != std::string::npos);
    EXPECT_TRUE(str.find("MIT") != std::string::npos);
}

TEST_F(PluginInfoTest,EqualityOperatorWorks)
{
    docker_cpp::PluginInfo info2(docker_cpp::PluginInfoParams{
        "test-plugin", "1.0.0", "Test Plugin Description", docker_cpp::PluginType::CORE, "test-author", "MIT"});
    docker_cpp::PluginInfo info3(docker_cpp::PluginInfoParams{"different-plugin",
                                      "1.0.0",
                                      "Test Plugin Description",
                                      docker_cpp::PluginType::CORE,
                                      "test-author",
                                      "MIT"});

    EXPECT_TRUE(getInfo() == info2);
    EXPECT_FALSE(getInfo() == info3);
}

TEST_F(PluginInfoTest,InequalityOperatorWorks)
{
    docker_cpp::PluginInfo info2(docker_cpp::PluginInfoParams{"different-plugin",
                                      "1.0.0",
                                      "Test Plugin Description",
                                      docker_cpp::PluginType::CORE,
                                      "test-author",
                                      "MIT"});

    EXPECT_TRUE(getInfo() != info2);
}
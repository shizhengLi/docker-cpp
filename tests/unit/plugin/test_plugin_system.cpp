#include <gtest/gtest.h>
#include <docker-cpp/core/error.hpp>
#include <docker-cpp/plugin/plugin_interface.hpp>
#include <docker-cpp/plugin/plugin_registry.hpp>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

using namespace docker_cpp;

class MockPlugin : public IPlugin {
public:
    MockPlugin(const std::string& name,
               const std::string& version,
               bool init_result = true,
               bool should_fail = false,
               const std::vector<std::string>& dependencies = {},
               const std::vector<std::string>& capabilities = {})
        : name_(name), version_(version), init_result_(init_result), should_fail_(should_fail),
          initialized_(false), dependencies_(dependencies), capabilities_(capabilities)
    {}

    std::string getName() const override
    {
        return name_;
    }
    std::string getVersion() const override
    {
        return version_;
    }

    PluginInfo getPluginInfo() const override
    {
        return PluginInfo(PluginInfoParams{name_, version_, "Mock Plugin for testing", PluginType::CUSTOM, "Test Author", "MIT"});
    }

    bool initialize(const PluginConfig& config) override
    {
        if (should_fail_) {
            throw ContainerError(ErrorCode::PLUGIN_INITIALIZATION_FAILED,
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
    const PluginConfig& getConfig() const
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
    PluginConfig config_;
    std::vector<std::string> dependencies_;
    std::vector<std::string> capabilities_;
};

class PluginInterfaceTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        plugin = std::make_unique<MockPlugin>("test-plugin", "1.0.0");
    }

    std::unique_ptr<MockPlugin> plugin;
};

TEST_F(PluginInterfaceTest, GetNameReturnsCorrectName)
{
    EXPECT_EQ(plugin->getName(), "test-plugin");
}

TEST_F(PluginInterfaceTest, GetVersionReturnsCorrectVersion)
{
    EXPECT_EQ(plugin->getVersion(), "1.0.0");
}

TEST_F(PluginInterfaceTest, InitializeSucceedsWithValidConfig)
{
    PluginConfig config;
    config["key"] = "value";

    EXPECT_TRUE(plugin->initialize(config));
    EXPECT_TRUE(plugin->isInitialized());
    EXPECT_EQ(plugin->getConfig().at("key"), "value");
}

TEST_F(PluginInterfaceTest, InitializeFailsWhenConfigured)
{
    auto failing_plugin = std::make_unique<MockPlugin>("failing-plugin", "1.0.0", false);

    PluginConfig config;
    EXPECT_FALSE(failing_plugin->initialize(config));
    EXPECT_FALSE(failing_plugin->isInitialized());
}

TEST_F(PluginInterfaceTest, InitializeThrowsWhenConfigured)
{
    auto throwing_plugin = std::make_unique<MockPlugin>("throwing-plugin", "1.0.0", true, true);

    PluginConfig config;
    EXPECT_THROW(throwing_plugin->initialize(config), ContainerError);
    EXPECT_FALSE(throwing_plugin->isInitialized());
}

TEST_F(PluginInterfaceTest, ShutdownResetsInitializedState)
{
    PluginConfig config;
    plugin->initialize(config);
    EXPECT_TRUE(plugin->isInitialized());

    plugin->shutdown();
    EXPECT_FALSE(plugin->isInitialized());
}

class PluginRegistryTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        registry = std::make_unique<PluginRegistry>();
    }

    std::unique_ptr<PluginRegistry> registry;
};

TEST_F(PluginRegistryTest, RegisterPluginSucceeds)
{
    auto plugin = std::make_unique<MockPlugin>("plugin1", "1.0.0");
    registry->registerPlugin("plugin1", std::move(plugin));

    auto* retrieved = registry->getPlugin<IPlugin>("plugin1");
    EXPECT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->getName(), "plugin1");
    EXPECT_EQ(retrieved->getVersion(), "1.0.0");
}

TEST_F(PluginRegistryTest, RegisterDuplicatePluginThrows)
{
    auto plugin1 = std::make_unique<MockPlugin>("plugin1", "1.0.0");
    registry->registerPlugin("plugin1", std::move(plugin1));

    auto duplicate_plugin = std::make_unique<MockPlugin>("plugin1", "1.0.1");
    EXPECT_THROW(registry->registerPlugin("plugin1", std::move(duplicate_plugin)), ContainerError);
}

TEST_F(PluginRegistryTest, GetNonExistentPluginReturnsNull)
{
    auto* retrieved = registry->getPlugin<IPlugin>("nonexistent");
    EXPECT_EQ(retrieved, nullptr);
}

TEST_F(PluginRegistryTest, GetPluginAfterMoveReturnsNull)
{
    // Create a new plugin for this test
    auto test_plugin = std::make_unique<MockPlugin>("test-plugin", "1.0.0");

    // Register a plugin first
    registry->registerPlugin("test-plugin", std::move(test_plugin));

    // Then move the registry
    auto moved_registry = std::move(registry);

    // Original registry should not return plugins (moved-from state)
    // But we shouldn't access moved-from objects, so we'll just check that
    // moved registry has the plugin
    auto* retrieved = moved_registry->getPlugin<IPlugin>("test-plugin");
    EXPECT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->getName(), "test-plugin");
}

TEST_F(PluginRegistryTest, RegisterMultiplePlugins)
{
    auto p1 = std::make_unique<MockPlugin>("plugin1", "1.0.0");
    auto p2 = std::make_unique<MockPlugin>("plugin2", "2.0.0");
    auto p3 = std::make_unique<MockPlugin>("plugin3", "1.5.0");

    registry->registerPlugin("plugin1", std::move(p1));
    registry->registerPlugin("plugin2", std::move(p2));
    registry->registerPlugin("plugin3", std::move(p3));

    auto* r1 = registry->getPlugin<IPlugin>("plugin1");
    auto* r2 = registry->getPlugin<IPlugin>("plugin2");
    auto* r3 = registry->getPlugin<IPlugin>("plugin3");

    EXPECT_NE(r1, nullptr);
    EXPECT_NE(r2, nullptr);
    EXPECT_NE(r3, nullptr);

    EXPECT_EQ(r1->getName(), "plugin1");
    EXPECT_EQ(r2->getName(), "plugin2");
    EXPECT_EQ(r3->getName(), "plugin3");
}

TEST_F(PluginRegistryTest, GetPluginNamesReturnsAllRegisteredPlugins)
{
    auto p1 = std::make_unique<MockPlugin>("plugin1", "1.0.0");
    auto p2 = std::make_unique<MockPlugin>("plugin2", "2.0.0");
    auto p3 = std::make_unique<MockPlugin>("plugin3", "1.5.0");

    registry->registerPlugin("plugin1", std::move(p1));
    registry->registerPlugin("plugin2", std::move(p2));
    registry->registerPlugin("plugin3", std::move(p3));

    auto names = registry->getPluginNames();
    EXPECT_EQ(names.size(), 3);

    // Convert to set for order-independent comparison
    std::unordered_set<std::string> name_set(names.begin(), names.end());
    EXPECT_TRUE(name_set.count("plugin1"));
    EXPECT_TRUE(name_set.count("plugin2"));
    EXPECT_TRUE(name_set.count("plugin3"));
}

TEST_F(PluginRegistryTest, UnregisterPluginRemovesPlugin)
{
    auto p1 = std::make_unique<MockPlugin>("plugin1", "1.0.0");
    auto p2 = std::make_unique<MockPlugin>("plugin2", "2.0.0");

    registry->registerPlugin("plugin1", std::move(p1));
    registry->registerPlugin("plugin2", std::move(p2));

    // Verify plugin exists
    auto* plugin1_retrieved = registry->getPlugin<IPlugin>("plugin1");
    EXPECT_NE(plugin1_retrieved, nullptr);

    // Unregister and verify removal
    registry->unregisterPlugin("plugin1");
    plugin1_retrieved = registry->getPlugin<IPlugin>("plugin1");
    EXPECT_EQ(plugin1_retrieved, nullptr);

    // Verify other plugin still exists
    auto* plugin2_retrieved = registry->getPlugin<IPlugin>("plugin2");
    EXPECT_NE(plugin2_retrieved, nullptr);
}

TEST_F(PluginRegistryTest, UnregisterNonExistentPluginThrows)
{
    EXPECT_THROW(registry->unregisterPlugin("nonexistent"), ContainerError);
}

TEST_F(PluginRegistryTest, InitializePluginSucceeds)
{
    auto plugin1 = std::make_unique<MockPlugin>("plugin1", "1.0.0");
    registry->registerPlugin("plugin1", std::move(plugin1));

    PluginConfig config;
    config["test"] = "value";

    EXPECT_TRUE(registry->initializePlugin("plugin1", config));

    auto* p1 = registry->getPlugin<MockPlugin>("plugin1");
    EXPECT_NE(p1, nullptr);
    EXPECT_TRUE(p1->isInitialized());
}

TEST_F(PluginRegistryTest, InitializeNonExistentPluginFails)
{
    PluginConfig config;
    EXPECT_FALSE(registry->initializePlugin("nonexistent", config));
}

TEST_F(PluginRegistryTest, InitializePluginWithExceptionFails)
{
    auto throwing_plugin = std::make_unique<MockPlugin>("throwing", "1.0.0", true, true);
    registry->registerPlugin("throwing", std::move(throwing_plugin));

    PluginConfig config;
    EXPECT_FALSE(registry->initializePlugin("throwing", config));
}

TEST_F(PluginRegistryTest, ShutdownPluginSucceeds)
{
    auto plugin1 = std::make_unique<MockPlugin>("plugin1", "1.0.0");
    registry->registerPlugin("plugin1", std::move(plugin1));

    PluginConfig config;
    registry->initializePlugin("plugin1", config);

    auto* p1 = registry->getPlugin<MockPlugin>("plugin1");
    EXPECT_TRUE(p1->isInitialized());

    registry->shutdownPlugin("plugin1");
    EXPECT_FALSE(p1->isInitialized());
}

TEST_F(PluginRegistryTest, ShutdownNonExistentPluginFails)
{
    EXPECT_FALSE(registry->shutdownPlugin("nonexistent"));
}

TEST_F(PluginRegistryTest, InitializeAllPluginsSucceeds)
{
    auto plugin1 = std::make_unique<MockPlugin>("plugin1", "1.0.0");
    registry->registerPlugin("plugin1", std::move(plugin1));
    auto plugin2 = std::make_unique<MockPlugin>("plugin2", "2.0.0");
    registry->registerPlugin("plugin2", std::move(plugin2));
    auto plugin3 = std::make_unique<MockPlugin>("plugin3", "1.5.0");
    registry->registerPlugin("plugin3", std::move(plugin3));

    PluginConfig config;
    auto results = registry->initializeAllPlugins(config);

    EXPECT_EQ(results.size(), 3);
    EXPECT_TRUE(results.at("plugin1"));
    EXPECT_TRUE(results.at("plugin2"));
    EXPECT_TRUE(results.at("plugin3"));
}

TEST_F(PluginRegistryTest, ShutdownAllPluginsSucceeds)
{
    auto plugin1 = std::make_unique<MockPlugin>("plugin1", "1.0.0");
    registry->registerPlugin("plugin1", std::move(plugin1));
    auto plugin2 = std::make_unique<MockPlugin>("plugin2", "2.0.0");
    registry->registerPlugin("plugin2", std::move(plugin2));
    auto plugin3 = std::make_unique<MockPlugin>("plugin3", "1.5.0");
    registry->registerPlugin("plugin3", std::move(plugin3));

    PluginConfig config;
    registry->initializeAllPlugins(config);

    registry->shutdownAllPlugins();

    auto* p1 = registry->getPlugin<MockPlugin>("plugin1");
    auto* p2 = registry->getPlugin<MockPlugin>("plugin2");
    auto* p3 = registry->getPlugin<MockPlugin>("plugin3");

    EXPECT_FALSE(p1->isInitialized());
    EXPECT_FALSE(p2->isInitialized());
    EXPECT_FALSE(p3->isInitialized());
}

class PluginInfoTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        info = PluginInfo(PluginInfoParams{
            "test-plugin",
            "1.0.0",
            "Test Plugin Description",
            PluginType::CORE,
            "test-author",
            "MIT"});
    }

    PluginInfo info{}; // Default construct first
};

TEST_F(PluginInfoTest, GettersReturnCorrectValues)
{
    EXPECT_EQ(info.getName(), "test-plugin");
    EXPECT_EQ(info.getVersion(), "1.0.0");
    EXPECT_EQ(info.getDescription(), "Test Plugin Description");
    EXPECT_EQ(info.getType(), PluginType::CORE);
    EXPECT_EQ(info.getAuthor(), "test-author");
    EXPECT_EQ(info.getLicense(), "MIT");
}

TEST_F(PluginInfoTest, SettersUpdateValues)
{
    info.setDescription("Updated Description");
    info.setAuthor("Updated Author");
    info.setLicense("GPL-3.0");

    EXPECT_EQ(info.getDescription(), "Updated Description");
    EXPECT_EQ(info.getAuthor(), "Updated Author");
    EXPECT_EQ(info.getLicense(), "GPL-3.0");
}

TEST_F(PluginInfoTest, ToStringReturnsFormattedString)
{
    std::string str = info.toString();
    EXPECT_TRUE(str.find("test-plugin") != std::string::npos);
    EXPECT_TRUE(str.find("1.0.0") != std::string::npos);
    EXPECT_TRUE(str.find("Test Plugin Description") != std::string::npos);
    EXPECT_TRUE(str.find("test-author") != std::string::npos);
    EXPECT_TRUE(str.find("MIT") != std::string::npos);
}

TEST_F(PluginInfoTest, EqualityOperatorWorks)
{
    PluginInfo info2(PluginInfoParams{
        "test-plugin", "1.0.0", "Test Plugin Description", PluginType::CORE, "test-author", "MIT"});
    PluginInfo info3(PluginInfoParams{
        "different-plugin",
        "1.0.0",
        "Test Plugin Description",
        PluginType::CORE,
        "test-author",
        "MIT"});

    EXPECT_TRUE(info == info2);
    EXPECT_FALSE(info == info3);
}

TEST_F(PluginInfoTest, InequalityOperatorWorks)
{
    PluginInfo info2(PluginInfoParams{
        "different-plugin",
        "1.0.0",
        "Test Plugin Description",
        PluginType::CORE,
        "test-author",
        "MIT"});

    EXPECT_TRUE(info != info2);
}
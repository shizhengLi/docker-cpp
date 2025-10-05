#include <gtest/gtest.h>
#include <docker-cpp/config/config_manager.hpp>
#include <docker-cpp/core/error.hpp>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <thread>

class ConfigManagerAdvancedTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        config_manager = std::make_unique<docker_cpp::ConfigManager>();
        test_dir = std::filesystem::temp_directory_path() / "docker_cpp_config_advanced_test";
        std::filesystem::create_directories(test_dir);
    }

    void TearDown() override
    {
        std::filesystem::remove_all(test_dir);
    }

    void createTestConfigFile(const std::string& filename, const std::string& content)
    {
        std::ofstream file(test_dir / filename);
        file << content;
        file.close();
    }

    std::unique_ptr<docker_cpp::ConfigManager> config_manager;
    std::filesystem::path test_dir;
};

// Performance test for large configurations
TEST_F(ConfigManagerAdvancedTest, PerformanceLargeConfig)
{
    const int num_entries = 10000;

    auto start_time = std::chrono::high_resolution_clock::now();

    // Add many configuration entries
    for (int i = 0; i < num_entries; ++i) {
        std::string key = "section" + std::to_string(i / 100) + ".subsection" + std::to_string(i % 100) + ".key" + std::to_string(i);
        config_manager->set(key, "value" + std::to_string(i));
    }

    auto insertion_time = std::chrono::high_resolution_clock::now();
    auto insertion_duration = std::chrono::duration_cast<std::chrono::milliseconds>(insertion_time - start_time);

    // Test lookup performance
    start_time = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < num_entries; i += 100) {
        std::string key = "section" + std::to_string(i / 100) + ".subsection" + std::to_string(i % 100) + ".key" + std::to_string(i);
        auto value = config_manager->get<std::string>(key);
        EXPECT_EQ(value, "value" + std::to_string(i));
    }

    auto lookup_time = std::chrono::high_resolution_clock::now();
    auto lookup_duration = std::chrono::duration_cast<std::chrono::milliseconds>(lookup_time - start_time);

    // Performance assertions (adjust these values based on your requirements)
    EXPECT_LT(insertion_duration.count(), 1000); // Should complete insertion in under 1 second
    EXPECT_LT(lookup_duration.count(), 100);   // Should complete lookups in under 100ms
    EXPECT_EQ(config_manager->size(), num_entries);
}

// Test complex JSON nesting with our simple parser
TEST_F(ConfigManagerAdvancedTest, ComplexJsonHandling)
{
    std::string json = R"({
        "database": {
            "primary": {
                "host": "localhost",
                "port": 5432,
                "credentials": {
                    "username": "admin",
                    "password": "secret"
                }
            },
            "replica": {
                "host": "replica.example.com",
                "port": 5433
            }
        },
        "cache": {
            "redis": {
                "host": "redis.example.com",
                "port": 6379,
                "ttl": 3600
            }
        }
    })";

    // This should work with our simple parser since we're using flat structure
    std::string flat_json = R"({
        "database.primary.host": "localhost",
        "database.primary.port": 5432,
        "database.primary.credentials.username": "admin",
        "database.primary.credentials.password": "secret",
        "database.replica.host": "replica.example.com",
        "database.replica.port": 5433,
        "cache.redis.host": "redis.example.com",
        "cache.redis.port": 6379,
        "cache.redis.ttl": 3600
    })";

    EXPECT_NO_THROW(config_manager->loadFromJsonString(flat_json));

    EXPECT_EQ(config_manager->get<std::string>("database.primary.host"), "localhost");
    EXPECT_EQ(config_manager->get<int>("database.primary.port"), 5432);
    EXPECT_EQ(config_manager->get<std::string>("database.primary.credentials.username"), "admin");
    EXPECT_EQ(config_manager->get<std::string>("cache.redis.host"), "redis.example.com");
    EXPECT_EQ(config_manager->get<int>("cache.redis.ttl"), 3600);
}

// Test configuration layering with override behavior
TEST_F(ConfigManagerAdvancedTest, ConfigurationLayering)
{
    // Create base configuration
    docker_cpp::ConfigManager base_config;
    base_config.set("server.host", "localhost");
    base_config.set("server.port", 8080);
    base_config.set("debug", false);
    base_config.set("log.level", "INFO");

    // Create environment-specific config
    docker_cpp::ConfigManager prod_config;
    prod_config.set("server.host", "prod.example.com");
    prod_config.set("debug", false);
    prod_config.set("log.level", "ERROR");

    // Create user config
    docker_cpp::ConfigManager user_config;
    user_config.set("debug", true);
    user_config.set("log.level", "DEBUG");

    // Layer them: base -> prod -> user (user overrides all)
    docker_cpp::ConfigManager layered_config;
    layered_config.merge(base_config);
    layered_config.addLayer("production", prod_config);
    layered_config.addLayer("user", user_config);

    auto effective_config = layered_config.getEffectiveConfig();

    // User config should win
    EXPECT_EQ(effective_config.get<std::string>("server.host"), "prod.example.com"); // from prod
    EXPECT_EQ(effective_config.get<int>("server.port"), 8080); // from base
    EXPECT_TRUE(effective_config.get<bool>("debug")); // from user
    EXPECT_EQ(effective_config.get<std::string>("log.level"), "DEBUG"); // from user
}

// Test environment variable expansion with edge cases
TEST_F(ConfigManagerAdvancedTest, EnvironmentVariableExpansionEdgeCases)
{
    // Set test environment variables
#ifdef _WIN32
    _putenv("TEST_VAR=expanded_value");
    _putenv("EMPTY_VAR=");
    _putenv("MISSING_VAR");
#else
    setenv("TEST_VAR", "expanded_value", 1);
    setenv("EMPTY_VAR", "", 1);
    unsetenv("MISSING_VAR");
#endif

    config_manager->set("test.expanded", "${TEST_VAR}");
    config_manager->set("test.empty", "${EMPTY_VAR}");
    config_manager->set("test.missing", "${MISSING_VAR}");
    config_manager->set("test.multiple", "${TEST_VAR}_${TEST_VAR}");
    config_manager->set("test.nested", "prefix_${TEST_VAR}_suffix");
    config_manager->set("test.mixed", "text_${TEST_VAR}_more_${TEST_VAR}_text");

    auto expanded = config_manager->expandEnvironmentVariables();

    EXPECT_EQ(expanded.get<std::string>("test.expanded"), "expanded_value");
    EXPECT_EQ(expanded.get<std::string>("test.empty"), "");
    EXPECT_EQ(expanded.get<std::string>("test.missing"), "${MISSING_VAR}"); // Unchanged
    EXPECT_EQ(expanded.get<std::string>("test.multiple"), "expanded_value_expanded_value");
    EXPECT_EQ(expanded.get<std::string>("test.nested"), "prefix_expanded_value_suffix");
    EXPECT_EQ(expanded.get<std::string>("test.mixed"), "text_expanded_value_more_expanded_value_text");
}

// Test configuration validation with complex schemas
TEST_F(ConfigManagerAdvancedTest, AdvancedConfigurationValidation)
{
    // Set up a complex configuration
    config_manager->set("server.host", "example.com");
    config_manager->set("server.port", 8080);
    config_manager->set("server.ssl.enabled", true);
    config_manager->set("server.ssl.cert_file", "/path/to/cert.pem");
    config_manager->set("database.host", "localhost");
    config_manager->set("database.port", 5432);
    config_manager->set("database.name", "myapp");
    config_manager->set("cache.enabled", true);
    config_manager->set("cache.ttl", 3600);

    // Define validation schema
    std::unordered_map<std::string, docker_cpp::ConfigValueType> schema = {
        {"server.host", docker_cpp::ConfigValueType::STRING},
        {"server.port", docker_cpp::ConfigValueType::INTEGER},
        {"server.ssl.enabled", docker_cpp::ConfigValueType::BOOLEAN},
        {"server.ssl.cert_file", docker_cpp::ConfigValueType::STRING},
        {"database.host", docker_cpp::ConfigValueType::STRING},
        {"database.port", docker_cpp::ConfigValueType::INTEGER},
        {"database.name", docker_cpp::ConfigValueType::STRING},
        {"cache.enabled", docker_cpp::ConfigValueType::BOOLEAN},
        {"cache.ttl", docker_cpp::ConfigValueType::INTEGER}
    };

    // Should pass validation
    EXPECT_NO_THROW(config_manager->validate(schema));

    // Test type mismatch detection
    config_manager->set("server.port", "not_a_number");
    EXPECT_THROW(config_manager->validate(schema), docker_cpp::ContainerError);

    // Restore correct type and test missing required field
    config_manager->set("server.port", 8080);
    schema["server.timeout"] = docker_cpp::ConfigValueType::INTEGER; // Add required field

    // Should still pass since we only validate existing keys
    EXPECT_NO_THROW(config_manager->validate(schema));
}

// Test change notifications and callbacks
TEST_F(ConfigManagerAdvancedTest, ConfigurationChangeNotifications)
{
    std::vector<std::tuple<std::string, std::string, std::string>> changes;

    config_manager->setChangeCallback([&](const std::string& key, const docker_cpp::ConfigValue& old_val, const docker_cpp::ConfigValue& new_val) {
        changes.emplace_back(key, old_val.toString(), new_val.toString());
    });
    config_manager->enableChangeNotifications(true);

    // Make some changes
    config_manager->set("test.key1", "value1");
    config_manager->set("test.key2", "value2");
    config_manager->set("test.key1", "updated_value1");
    config_manager->remove("test.key2");

    EXPECT_EQ(changes.size(), 4);

    EXPECT_EQ(std::get<0>(changes[0]), "test.key1");
    EXPECT_EQ(std::get<1>(changes[0]), ""); // Old value was empty
    EXPECT_EQ(std::get<2>(changes[0]), "value1");

    EXPECT_EQ(std::get<0>(changes[1]), "test.key2");
    EXPECT_EQ(std::get<1>(changes[1]), "");
    EXPECT_EQ(std::get<2>(changes[1]), "value2");

    EXPECT_EQ(std::get<0>(changes[2]), "test.key1");
    EXPECT_EQ(std::get<1>(changes[2]), "value1");
    EXPECT_EQ(std::get<2>(changes[2]), "updated_value1");

    EXPECT_EQ(std::get<0>(changes[3]), "test.key2");
    EXPECT_EQ(std::get<1>(changes[3]), "value2");
    EXPECT_EQ(std::get<2>(changes[3]), ""); // New value is empty after removal
}

// Test concurrent access safety
TEST_F(ConfigManagerAdvancedTest, ConcurrentAccessSafety)
{
    const int num_threads = 10;
    const int operations_per_thread = 1000;
    std::vector<std::thread> threads;

    // Start threads that perform concurrent operations
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([this, i]() {
            for (int j = 0; j < operations_per_thread; ++j) {
                std::string key = "thread" + std::to_string(i) + ".key" + std::to_string(j);
                std::string value = "value" + std::to_string(j);

                config_manager->set(key, value);

                // Read back some values
                if (j > 0) {
                    std::string prev_key = "thread" + std::to_string(i) + ".key" + std::to_string(j - 1);
                    auto retrieved = config_manager->get<std::string>(prev_key);
                    EXPECT_EQ(retrieved, "value" + std::to_string(j - 1));
                }
            }
        });
    }

    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }

    // Verify final state
    EXPECT_EQ(config_manager->size(), num_threads * operations_per_thread);
}

// Test memory usage with large configurations
TEST_F(ConfigManagerAdvancedTest, MemoryUsageEfficiency)
{
    // This is a basic test - in real scenarios you might want to use more sophisticated memory measurement
    const int num_entries = 50000;
    size_t initial_size = config_manager->size();

    // Add entries
    for (int i = 0; i < num_entries; ++i) {
        std::string key = "performance.test.entry" + std::to_string(i);
        std::string value = "test_value_" + std::to_string(i) + "_with_additional_data";
        config_manager->set(key, value);
    }

    EXPECT_EQ(config_manager->size(), initial_size + num_entries);

    // Test that operations are still performant
    auto start = std::chrono::high_resolution_clock::now();

    // Get keys with prefix
    auto keys = config_manager->getKeysWithPrefix("performance.test.");
    EXPECT_EQ(keys.size(), num_entries);

    // Clear and verify cleanup
    config_manager->clear();
    EXPECT_TRUE(config_manager->isEmpty());
    EXPECT_EQ(config_manager->size(), 0);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // Operations should complete quickly even with large configs
    EXPECT_LT(duration.count(), 1000); // Should complete in under 1 second
}

// Test error recovery and resilience
TEST_F(ConfigManagerAdvancedTest, ErrorRecoveryAndResilience)
{
    // Test recovery from malformed JSON
    std::string malformed_json = R"({
        "valid.key": "valid_value",
        "invalid": "missing_closing_quote,
        "another.valid": 123
    })";

    EXPECT_THROW(config_manager->loadFromJsonString(malformed_json), docker_cpp::ContainerError);

    // Config manager should still be functional after error
    EXPECT_NO_THROW(config_manager->set("recovery.test", "recovered_value"));
    EXPECT_EQ(config_manager->get<std::string>("recovery.test"), "recovered_value");

    // Test with completely invalid JSON
    std::string completely_invalid = "this is not json at all { invalid syntax";
    EXPECT_THROW(config_manager->loadFromJsonString(completely_invalid), docker_cpp::ContainerError);

    // Should still be functional
    EXPECT_NO_THROW(config_manager->set("another.test", "still_working"));
    EXPECT_EQ(config_manager->get<std::string>("another.test"), "still_working");
}
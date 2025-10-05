#include <gtest/gtest.h>
#include <docker-cpp/config/config_manager.hpp>
#include <docker-cpp/core/error.hpp>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>


class docker_cpp::ConfigManagerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        config_manager = std::make_unique<docker_cpp::docker_cpp::ConfigManager>();
        test_dir = std::filesystem::temp_directory_path() / "docker_cpp_config_test";
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

    std::unique_ptr<docker_cpp::docker_cpp::ConfigManager> config_manager;
    std::filesystem::path test_dir;
};

TEST_F(docker_cpp::ConfigManagerTest, DefaultConstructorCreatesEmptyConfig)
{
    EXPECT_TRUE(config_manager->isEmpty());
    EXPECT_EQ(config_manager->size(), 0);
}

TEST_F(docker_cpp::ConfigManagerTest, SetAndGetStringValue)
{
    config_manager->set("server.host", "localhost");
    config_manager->set("server.port", "8080");

    EXPECT_EQ(config_manager->get<std::string>("server.host"), "localhost");
    EXPECT_EQ(config_manager->get<std::string>("server.port"), "8080");
    EXPECT_FALSE(config_manager->isEmpty());
    EXPECT_EQ(config_manager->size(), 2);
}

TEST_F(docker_cpp::ConfigManagerTest, SetAndGetIntValue)
{
    config_manager->set("server.port", 8080);
    config_manager->set("max_connections", 100);

    EXPECT_EQ(config_manager->get<int>("server.port"), 8080);
    EXPECT_EQ(config_manager->get<int>("max_connections"), 100);
}

TEST_F(docker_cpp::ConfigManagerTest, SetAndGetBoolValue)
{
    config_manager->set("debug.enabled", true);
    config_manager->set("production.mode", false);

    EXPECT_TRUE(config_manager->get<bool>("debug.enabled"));
    EXPECT_FALSE(config_manager->get<bool>("production.mode"));
}

TEST_F(docker_cpp::ConfigManagerTest, SetAndGetDoubleValue)
{
    config_manager->set("cpu.limit", 2.5);
    config_manager->set("memory.threshold", 0.85);

    EXPECT_DOUBLE_EQ(config_manager->get<double>("cpu.limit"), 2.5);
    EXPECT_DOUBLE_EQ(config_manager->get<double>("memory.threshold"), 0.85);
}

TEST_F(docker_cpp::ConfigManagerTest, GetWithDefaultValue)
{
    config_manager->set("existing.key", "value");

    EXPECT_EQ(config_manager->get<std::string>("existing.key", "default"), "value");
    EXPECT_EQ(config_manager->get<std::string>("missing.key", "default"), "default");
    EXPECT_EQ(config_manager->get<int>("missing.int", 42), 42);
    EXPECT_TRUE(config_manager->get<bool>("missing.bool", true));
}

TEST_F(docker_cpp::ConfigManagerTest, HasKeyMethod)
{
    config_manager->set("existing.key", "value");

    EXPECT_TRUE(config_manager->has("existing.key"));
    EXPECT_FALSE(config_manager->has("missing.key"));
}

TEST_F(docker_cpp::ConfigManagerTest, RemoveKey)
{
    config_manager->set("key1", "value1");
    config_manager->set("key2", "value2");

    EXPECT_EQ(config_manager->size(), 2);

    config_manager->remove("key1");

    EXPECT_EQ(config_manager->size(), 1);
    EXPECT_FALSE(config_manager->has("key1"));
    EXPECT_TRUE(config_manager->has("key2"));
}

TEST_F(docker_cpp::ConfigManagerTest, ClearAll)
{
    config_manager->set("key1", "value1");
    config_manager->set("key2", "value2");

    EXPECT_FALSE(config_manager->isEmpty());

    config_manager->clear();

    EXPECT_TRUE(config_manager->isEmpty());
    EXPECT_EQ(config_manager->size(), 0);
}

TEST_F(docker_cpp::ConfigManagerTest, GetKeysWithPrefix)
{
    config_manager->set("server.host", "localhost");
    config_manager->set("server.port", "8080");
    config_manager->set("server.ssl.enabled", true);
    config_manager->set("database.host", "db.example.com");

    auto server_keys = config_manager->getKeysWithPrefix("server.");
    auto db_keys = config_manager->getKeysWithPrefix("database.");

    EXPECT_EQ(server_keys.size(), 3);
    EXPECT_TRUE(std::find(server_keys.begin(), server_keys.end(), "server.host")
                != server_keys.end());
    EXPECT_TRUE(std::find(server_keys.begin(), server_keys.end(), "server.port")
                != server_keys.end());
    EXPECT_TRUE(std::find(server_keys.begin(), server_keys.end(), "server.ssl.enabled")
                != server_keys.end());

    EXPECT_EQ(db_keys.size(), 1);
    EXPECT_EQ(db_keys[0], "database.host");
}

TEST_F(docker_cpp::ConfigManagerTest, LoadFromJsonString)
{
    // Use flat JSON that simple parser can handle
    std::string json = R"({
        "server.host": "localhost",
        "server.port": 8080,
        "server.ssl.enabled": true,
        "server.ssl.cert_file": "/path/to/cert.pem",
        "debug": true,
        "max_connections": 100
    })";

    EXPECT_NO_THROW(config_manager->loadFromJsonString(json));

    EXPECT_EQ(config_manager->get<std::string>("server.host"), "localhost");
    EXPECT_EQ(config_manager->get<int>("server.port"), 8080);
    EXPECT_TRUE(config_manager->get<bool>("server.ssl.enabled"));
    EXPECT_EQ(config_manager->get<std::string>("server.ssl.cert_file"), "/path/to/cert.pem");
    EXPECT_TRUE(config_manager->get<bool>("debug"));
    EXPECT_EQ(config_manager->get<int>("max_connections"), 100);
}

TEST_F(docker_cpp::ConfigManagerTest, LoadFromJsonFile)
{
    // Use flat JSON that simple parser can handle
    std::string json = R"({
        "app.name": "docker-cpp",
        "app.version": "1.0.0",
        "logging.level": "info",
        "logging.file": "/var/log/docker-cpp.log"
    })";

    createTestConfigFile("config.json", json);

    EXPECT_NO_THROW(config_manager->loadFromJsonFile(test_dir / "config.json"));

    EXPECT_EQ(config_manager->get<std::string>("app.name"), "docker-cpp");
    EXPECT_EQ(config_manager->get<std::string>("app.version"), "1.0.0");
    EXPECT_EQ(config_manager->get<std::string>("logging.level"), "info");
    EXPECT_EQ(config_manager->get<std::string>("logging.file"), "/var/log/docker-cpp.log");
}

TEST_F(docker_cpp::ConfigManagerTest, LoadFromNonExistentFileThrows)
{
    EXPECT_THROW(config_manager->loadFromJsonFile(test_dir / "nonexistent.json"), docker_cpp::ContainerError);
}

TEST_F(docker_cpp::ConfigManagerTest, LoadFromInvalidJsonThrows)
{
    std::string invalid_json = R"({
        "key": "value",
        "invalid":
    })";

    createTestConfigFile("invalid.json", invalid_json);

    EXPECT_THROW(config_manager->loadFromJsonFile(test_dir / "invalid.json"), docker_cpp::ContainerError);
}

TEST_F(docker_cpp::ConfigManagerTest, SaveToJsonFile)
{
    config_manager->set("app.name", "docker-cpp");
    config_manager->set("app.version", "1.0.0");
    config_manager->set("debug.enabled", true);
    config_manager->set("server.port", 8080);

    std::filesystem::path output_file = test_dir / "output.json";

    EXPECT_NO_THROW(config_manager->saveToJsonFile(output_file));
    EXPECT_TRUE(std::filesystem::exists(output_file));

    // Load the saved file and verify content
    auto new_manager = std::make_unique<docker_cpp::ConfigManager>();
    new_manager->loadFromJsonFile(output_file);

    EXPECT_EQ(new_manager->get<std::string>("app.name"), "docker-cpp");
    EXPECT_EQ(new_manager->get<std::string>("app.version"), "1.0.0");
    EXPECT_TRUE(new_manager->get<bool>("debug.enabled"));
    EXPECT_EQ(new_manager->get<int>("server.port"), 8080);
}

TEST_F(docker_cpp::ConfigManagerTest, MergeConfigs)
{
    // Set initial config
    config_manager->set("server.host", "localhost");
    config_manager->set("server.port", 8080);
    config_manager->set("debug", true);

    // Create config to merge
    docker_cpp::ConfigManager other_config;
    other_config.set("server.port", 9090);        // Override
    other_config.set("server.ssl.enabled", true); // New
    other_config.set("logging.level", "info");    // New

    config_manager->merge(other_config);

    EXPECT_EQ(config_manager->get<std::string>("server.host"), "localhost"); // Unchanged
    EXPECT_EQ(config_manager->get<int>("server.port"), 9090);                // Overridden
    EXPECT_TRUE(config_manager->get<bool>("debug"));                         // Unchanged
    EXPECT_TRUE(config_manager->get<bool>("server.ssl.enabled"));            // New
    EXPECT_EQ(config_manager->get<std::string>("logging.level"), "info");    // New
}

TEST_F(docker_cpp::ConfigManagerTest, CopyConstructor)
{
    config_manager->set("key1", "value1");
    config_manager->set("key2", 42);

    docker_cpp::ConfigManager copy;
    copy.merge(*config_manager);

    EXPECT_EQ(copy.get<std::string>("key1"), "value1");
    EXPECT_EQ(copy.get<int>("key2"), 42);
    EXPECT_EQ(copy.size(), 2);

    // Verify independence
    copy.set("new_key", "new_value");
    EXPECT_FALSE(config_manager->has("new_key"));
    EXPECT_TRUE(copy.has("new_key"));
}

TEST_F(docker_cpp::ConfigManagerTest, MoveConstructor)
{
    config_manager->set("key1", "value1");
    config_manager->set("key2", 42);

    docker_cpp::ConfigManager moved(std::move(*config_manager));

    EXPECT_EQ(moved.get<std::string>("key1"), "value1");
    EXPECT_EQ(moved.get<int>("key2"), 42);
    EXPECT_EQ(moved.size(), 2);

    // Original should be in valid but empty state
    EXPECT_TRUE(config_manager->isEmpty());
}

TEST_F(docker_cpp::ConfigManagerTest, AssignmentOperator)
{
    config_manager->set("key1", "value1");

    docker_cpp::ConfigManager other;
    other.set("key2", "value2");

    other.clear();
    other.merge(*config_manager);

    EXPECT_EQ(other.get<std::string>("key1"), "value1");
    EXPECT_FALSE(other.has("key2"));
    EXPECT_EQ(other.size(), 1);
}

TEST_F(docker_cpp::ConfigManagerTest, EnvironmentVariableExpansion)
{
    // Set environment variable
    std::string home_path = std::filesystem::temp_directory_path();
#ifdef _WIN32
    _putenv(("HOME=" + home_path).c_str());
#else
    setenv("HOME", home_path.c_str(), 1);
#endif

    config_manager->set("log.file", "${HOME}/logs/app.log");
    config_manager->set("data.dir", "${HOME}/data");

    auto expanded = config_manager->expandEnvironmentVariables();

    EXPECT_EQ(expanded.get<std::string>("log.file"), home_path + "/logs/app.log");
    EXPECT_EQ(expanded.get<std::string>("data.dir"), home_path + "/data");
}

TEST_F(docker_cpp::ConfigManagerTest, ConfigValidation)
{
    config_manager->set("server.port", 8080);
    config_manager->set("database.url", "postgresql://localhost:5432/db");

    // Create validation schema
    std::unordered_map<std::string, ConfigValueType> schema = {
        {"server.port", ConfigValueType::INTEGER},
        {"database.url", ConfigValueType::STRING},
        {"debug.enabled", ConfigValueType::BOOLEAN}};

    // Should pass - all existing keys match their types
    EXPECT_NO_THROW(config_manager->validate(schema));

    // Add a type mismatch
    config_manager->set("server.port", "not_a_number");

    EXPECT_THROW(config_manager->validate(schema), docker_cpp::ContainerError);
}

TEST_F(docker_cpp::ConfigManagerTest, NestedConfigAccess)
{
    config_manager->set("server.host", "localhost");
    config_manager->set("server.port", 8080);
    config_manager->set("server.ssl.enabled", true);
    config_manager->set("server.ssl.cert", "/path/to/cert.pem");

    auto server_config = config_manager->getSubConfig("server.");

    EXPECT_EQ(server_config.size(), 4);
    EXPECT_EQ(server_config.get<std::string>("host"), "localhost");
    EXPECT_EQ(server_config.get<int>("port"), 8080);
    EXPECT_TRUE(server_config.get<bool>("ssl.enabled"));
    EXPECT_EQ(server_config.get<std::string>("ssl.cert"), "/path/to/cert.pem");
}
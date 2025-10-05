#include <gtest/gtest.h>
#include "runtime/container_config.hpp"

using namespace dockercpp::runtime;

class ContainerConfigTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        config_ = std::make_unique<ContainerConfig>();
        config_->name = "test-container";
        config_->image = "ubuntu:latest";
        config_->command = {"/bin/echo", "hello world"};
        config_->working_dir = "/app";
    }

    std::unique_ptr<ContainerConfig> config_;
};

TEST_F(ContainerConfigTest, IsValidBasicConfiguration)
{
    EXPECT_TRUE(config_->isValid());
    EXPECT_TRUE(config_->validate().empty());
}

TEST_F(ContainerConfigTest, ValidationMissingImage)
{
    config_->image.clear();
    EXPECT_FALSE(config_->isValid());

    auto errors = config_->validate();
    EXPECT_EQ(errors.size(), 1);
    EXPECT_EQ(errors[0], "Container image is required");
}

TEST_F(ContainerConfigTest, ValidationMissingName)
{
    config_->name.clear();
    EXPECT_FALSE(config_->isValid());

    auto errors = config_->validate();
    EXPECT_EQ(errors.size(), 1);
    EXPECT_EQ(errors[0], "Container name is required");
}

TEST_F(ContainerConfigTest, ValidationInvalidName)
{
    config_->name = "invalid@name";
    EXPECT_FALSE(config_->isValid());

    auto errors = config_->validate();
    EXPECT_EQ(errors.size(), 1);
    EXPECT_EQ(errors[0], "Invalid container name: invalid@name");
}

TEST_F(ContainerConfigTest, ValidationInvalidWorkingDirectory)
{
    config_->working_dir = "relative/path";
    EXPECT_FALSE(config_->isValid());

    auto errors = config_->validate();
    EXPECT_EQ(errors.size(), 1);
    EXPECT_EQ(errors[0], "Working directory must be an absolute path: relative/path");
}

TEST_F(ContainerConfigTest, ValidationInvalidEnvironmentVariable)
{
    config_->env.push_back("INVALID_FORMAT");
    EXPECT_FALSE(config_->isValid());

    auto errors = config_->validate();
    EXPECT_EQ(errors.size(), 1);
    EXPECT_EQ(errors[0],
              "Invalid environment variable format: INVALID_FORMAT (should be KEY=VALUE)");
}

TEST_F(ContainerConfigTest, ValidationInvalidCpuSettings)
{
    config_->resources.cpu_period = 100000;
    config_->resources.cpu_quota = 200000; // Greater than period
    EXPECT_FALSE(config_->isValid());

    auto errors = config_->validate();
    EXPECT_EQ(errors.size(), 1);
    EXPECT_EQ(errors[0], "CPU quota cannot be greater than CPU period");
}

TEST_F(ContainerConfigTest, ValidationInvalidMemoryLimits)
{
    config_->resources.memory_limit = 1024 * 1024 * 1024;     // 1GB
    config_->resources.memory_swap_limit = 512 * 1024 * 1024; // 512MB
    EXPECT_FALSE(config_->isValid());

    auto errors = config_->validate();
    EXPECT_EQ(errors.size(), 1);
    EXPECT_EQ(errors[0], "Memory swap limit cannot be less than memory limit");
}

TEST_F(ContainerConfigTest, ValidationInvalidUserFormat)
{
    config_->security.user = "invalid@user";
    EXPECT_FALSE(config_->isValid());

    auto errors = config_->validate();
    EXPECT_EQ(errors.size(), 1);
    EXPECT_EQ(errors[0], "Invalid user format: invalid@user (should be uid:gid or username)");
}

TEST_F(ContainerConfigTest, ValidationInvalidPortMapping)
{
    config_->network.port_mappings.push_back({"", 0, "127.0.0.1", 8080, "tcp"});
    EXPECT_FALSE(config_->isValid());

    auto errors = config_->validate();
    EXPECT_EQ(errors.size(), 1);
    EXPECT_EQ(errors[0], "Container port cannot be 0 in port mapping");
}

TEST_F(ContainerConfigTest, ValidationInvalidPortProtocol)
{
    config_->network.port_mappings.push_back({"", 80, "127.0.0.1", 8080, "invalid"});
    EXPECT_FALSE(config_->isValid());

    auto errors = config_->validate();
    EXPECT_EQ(errors.size(), 1);
    EXPECT_EQ(errors[0], "Invalid protocol in port mapping: invalid (should be tcp or udp)");
}

TEST_F(ContainerConfigTest, EnvironmentVariableOperations)
{
    // Set environment variable
    config_->setEnvironment("TEST_KEY", "test_value");
    EXPECT_EQ(config_->getEnvironment("TEST_KEY"), "test_value");

    // Update existing environment variable
    config_->setEnvironment("TEST_KEY", "new_value");
    EXPECT_EQ(config_->getEnvironment("TEST_KEY"), "new_value");

    // Get non-existent environment variable
    EXPECT_TRUE(config_->getEnvironment("NON_EXISTENT").empty());
}

TEST_F(ContainerConfigTest, LabelOperations)
{
    // Add label
    config_->labels["version"] = "1.0";
    EXPECT_TRUE(config_->hasLabel("version"));
    EXPECT_EQ(config_->getLabel("version"), "1.0");

    // Get non-existent label
    EXPECT_FALSE(config_->hasLabel("non_existent"));
    EXPECT_TRUE(config_->getLabel("non_existent").empty());
}

TEST_F(ContainerConfigTest, DefaultValues)
{
    ContainerConfig default_config;

    // Check default resource limits
    EXPECT_EQ(default_config.resources.memory_limit, 0);
    EXPECT_EQ(default_config.resources.cpu_shares, 1.0);
    EXPECT_EQ(default_config.resources.cpu_period, 100000);
    EXPECT_EQ(default_config.resources.cpu_quota, 0);
    EXPECT_TRUE(default_config.resources.cpus.empty());
    EXPECT_EQ(default_config.resources.pids_limit, 0);

    // Check default security settings
    EXPECT_FALSE(default_config.security.read_only_rootfs);
    EXPECT_TRUE(default_config.security.no_new_privileges);
    EXPECT_EQ(default_config.security.umask, "0022");

    // Check default interactive settings
    EXPECT_FALSE(default_config.interactive);
    EXPECT_FALSE(default_config.tty);
    EXPECT_FALSE(default_config.attach_stdin);
    EXPECT_TRUE(default_config.attach_stdout);
    EXPECT_TRUE(default_config.attach_stderr);

    // Check default restart policy
    EXPECT_EQ(default_config.restart_policy.policy, RestartPolicy::NO);
    EXPECT_EQ(default_config.restart_policy.max_retries, 0);
    EXPECT_EQ(default_config.restart_policy.timeout, 10);
}

TEST_F(ContainerConfigTest, ComplexConfiguration)
{
    // Create a comprehensive configuration
    config_->env = {"PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
                    "HOME=/root",
                    "TERM=xterm"};
    config_->labels = {
        "maintainer", "docker-cpp-team", "version", "1.0.0", "description", "Test container"};
    config_->resources.memory_limit = 512 * 1024 * 1024; // 512MB
    config_->resources.cpu_shares = 2.0;
    config_->resources.cpu_quota = 50000;
    config_->resources.cpu_period = 100000;
    config_->security.user = "1000:1000";
    config_->security.capabilities = {"CAP_NET_ADMIN", "CAP_SYS_ADMIN"};
    config_->network.port_mappings.push_back({"", 80, "0.0.0.0", 8080, "tcp"});
    config_->network.port_mappings.push_back({"", 443, "0.0.0.0", 8443, "tcp"});
    config_->storage.volume_mounts.push_back({"/host/data", "/container/data", "bind", false});
    config_->storage.volume_mounts.push_back({"my-volume", "/app/data", "volume", false});
    config_->restart_policy.policy = RestartPolicy::ON_FAILURE;
    config_->restart_policy.max_retries = 3;

    // Validate the configuration
    EXPECT_TRUE(config_->isValid());
    EXPECT_TRUE(config_->validate().empty());

    // Check specific values
    EXPECT_EQ(config_->getEnvironment("PATH"),
              "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin");
    EXPECT_EQ(config_->getLabel("version"), "1.0.0");
    EXPECT_EQ(config_->resources.memory_limit, 512 * 1024 * 1024);
    EXPECT_EQ(config_->network.port_mappings.size(), 2);
    EXPECT_EQ(config_->storage.volume_mounts.size(), 2);
}

// ContainerInfo tests
class ContainerInfoTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        info_ = std::make_unique<ContainerInfo>();
        info_->id = "1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef";
        info_->name = "test-container";
        info_->image = "ubuntu:latest";
        info_->state = ContainerState::RUNNING;
        info_->created_at = std::chrono::system_clock::now();
        info_->started_at = info_->created_at + std::chrono::seconds(10);
        info_->pid = 12345;
    }

    std::unique_ptr<ContainerInfo> info_;
};

TEST_F(ContainerInfoTest, IsRunning)
{
    info_->state = ContainerState::RUNNING;
    EXPECT_TRUE(info_->isRunning());

    info_->state = ContainerState::STOPPED;
    EXPECT_FALSE(info_->isRunning());

    info_->state = ContainerState::PAUSED;
    EXPECT_FALSE(info_->isRunning());
}

TEST_F(ContainerInfoTest, GetUptimeRunning)
{
    info_->state = ContainerState::RUNNING;
    info_->started_at = std::chrono::system_clock::now() - std::chrono::seconds(100);

    auto uptime = info_->getUptime();
    EXPECT_GE(uptime.count(), 99); // Should be approximately 100 seconds
    EXPECT_LE(uptime.count(), 101);
}

TEST_F(ContainerInfoTest, GetUptimeStopped)
{
    info_->state = ContainerState::STOPPED;
    info_->started_at = std::chrono::system_clock::now() - std::chrono::seconds(200);
    info_->finished_at = std::chrono::system_clock::now() - std::chrono::seconds(100);

    auto uptime = info_->getUptime();
    EXPECT_EQ(uptime.count(), 100); // Should be exactly 100 seconds
}

TEST_F(ContainerInfoTest, GetUptimeNotStarted)
{
    info_->state = ContainerState::CREATED;
    // Don't set started_at

    auto uptime = info_->getUptime();
    EXPECT_EQ(uptime.count(), 0);
}

// Utility function tests
TEST(ContainerConfigUtilityTest, ContainerStateToString)
{
    EXPECT_EQ(containerStateToString(ContainerState::CREATED), "created");
    EXPECT_EQ(containerStateToString(ContainerState::RUNNING), "running");
    EXPECT_EQ(containerStateToString(ContainerState::STOPPED), "stopped");
    EXPECT_EQ(containerStateToString(ContainerState::ERROR), "error");
}

TEST(ContainerConfigUtilityTest, StringToContainerState)
{
    EXPECT_EQ(stringToContainerState("created"), ContainerState::CREATED);
    EXPECT_EQ(stringToContainerState("running"), ContainerState::RUNNING);
    EXPECT_EQ(stringToContainerState("stopped"), ContainerState::STOPPED);
    EXPECT_EQ(stringToContainerState("error"), ContainerState::ERROR);

    // Case insensitive
    EXPECT_EQ(stringToContainerState("RUNNING"), ContainerState::RUNNING);
    EXPECT_EQ(stringToContainerState("Stopped"), ContainerState::STOPPED);

    // Unknown state defaults to ERROR
    EXPECT_EQ(stringToContainerState("unknown"), ContainerState::ERROR);
}

TEST(ContainerConfigUtilityTest, GenerateContainerId)
{
    std::string id1 = generateContainerId();
    std::string id2 = generateContainerId();

    // Check length
    EXPECT_EQ(id1.length(), 64);
    EXPECT_EQ(id2.length(), 64);

    // Check uniqueness
    EXPECT_NE(id1, id2);

    // Check format (hexadecimal)
    EXPECT_TRUE(std::all_of(id1.begin(), id1.end(), ::isxdigit));
    EXPECT_TRUE(std::all_of(id2.begin(), id2.end(), ::isxdigit));

    // Check validity
    EXPECT_TRUE(isValidContainerId(id1));
    EXPECT_TRUE(isValidContainerId(id2));
}

TEST(ContainerConfigUtilityTest, GenerateContainerName)
{
    std::string name1 = generateContainerName();
    std::string name2 = generateContainerName();

    // Check uniqueness
    EXPECT_NE(name1, name2);

    // Check prefix
    EXPECT_TRUE(name1.find("docker-cpp-") == 0);
    EXPECT_TRUE(name2.find("docker-cpp-") == 0);

    // Check length (prefix + 6 random chars)
    EXPECT_EQ(name1.length(), 6 + 6);
    EXPECT_EQ(name2.length(), 6 + 6);

    // Check validity
    EXPECT_TRUE(isValidContainerName(name1));
    EXPECT_TRUE(isValidContainerName(name2));
}

TEST(ContainerConfigUtilityTest, GenerateContainerNameWithPrefix)
{
    std::string name = generateContainerName("test-");

    // Check prefix
    EXPECT_TRUE(name.find("test-") == 0);

    // Check length (prefix + 6 random chars)
    EXPECT_EQ(name.length(), 5 + 6);

    // Check validity
    EXPECT_TRUE(isValidContainerName(name));
}

TEST(ContainerConfigUtilityTest, IsValidContainerName)
{
    // Valid names
    EXPECT_TRUE(isValidContainerName("test-container"));
    EXPECT_TRUE(isValidContainerName("my_app"));
    EXPECT_TRUE(isValidContainerName("container123"));
    EXPECT_TRUE(isValidContainerName("a.b-c_d"));

    // Invalid names
    EXPECT_FALSE(isValidContainerName(""));                   // Empty
    EXPECT_FALSE(isValidContainerName("123container"));       // Starts with digit
    EXPECT_FALSE(isValidContainerName("container@name"));     // Invalid character
    EXPECT_FALSE(isValidContainerName("container name"));     // Space
    EXPECT_FALSE(isValidContainerName(std::string(64, 'a'))); // Too long
}

TEST(ContainerConfigUtilityTest, IsValidContainerId)
{
    // Valid IDs
    EXPECT_TRUE(
        isValidContainerId("1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef"));
    EXPECT_TRUE(isValidContainerId(std::string(64, 'f')));

    // Invalid IDs
    EXPECT_FALSE(isValidContainerId(""));                   // Empty
    EXPECT_FALSE(isValidContainerId("123"));                // Too short
    EXPECT_FALSE(isValidContainerId(std::string(63, 'a'))); // Too short
    EXPECT_FALSE(isValidContainerId(std::string(65, 'a'))); // Too long
    EXPECT_FALSE(isValidContainerId(
        "g1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcde")); // Invalid character
    EXPECT_FALSE(isValidContainerId(
        "G1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef")); // Uppercase
}

// ResourceStats tests
class ResourceStatsTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        stats_ = std::make_unique<ResourceStats>();
    }

    std::unique_ptr<ResourceStats> stats_;
};

TEST_F(ResourceStatsTest, DefaultValues)
{
    // Check default resource statistics
    EXPECT_EQ(stats_->cpu_usage_percent, 0.0);
    EXPECT_EQ(stats_->cpu_time_nanos, 0);
    EXPECT_EQ(stats_->system_cpu_time_nanos, 0);
    EXPECT_EQ(stats_->memory_usage_bytes, 0);
    EXPECT_EQ(stats_->memory_limit_bytes, 0);
    EXPECT_EQ(stats_->memory_cache_bytes, 0);
    EXPECT_EQ(stats_->memory_swap_usage_bytes, 0);
    EXPECT_EQ(stats_->memory_swap_limit_bytes, 0);
    EXPECT_EQ(stats_->current_pids, 0);
    EXPECT_EQ(stats_->pids_limit, 0);
    EXPECT_EQ(stats_->blkio_read_bytes, 0);
    EXPECT_EQ(stats_->blkio_write_bytes, 0);
    EXPECT_EQ(stats_->blkio_read_operations, 0);
    EXPECT_EQ(stats_->blkio_write_operations, 0);
    EXPECT_EQ(stats_->network_rx_bytes, 0);
    EXPECT_EQ(stats_->network_tx_bytes, 0);
    EXPECT_EQ(stats_->network_rx_packets, 0);
    EXPECT_EQ(stats_->network_tx_packets, 0);
    EXPECT_EQ(stats_->network_rx_errors, 0);
    EXPECT_EQ(stats_->network_tx_errors, 0);

    // Check timestamp is set to current time
    auto now = std::chrono::system_clock::now();
    auto diff = std::chrono::duration_cast<std::chrono::seconds>(now - stats_->timestamp);
    EXPECT_LT(diff.count(), 1); // Should be within 1 second
}

TEST_F(ResourceStatsTest, SetValues)
{
    // Set some values
    stats_->cpu_usage_percent = 50.5;
    stats_->memory_usage_bytes = 1024 * 1024 * 100; // 100MB
    stats_->memory_limit_bytes = 1024 * 1024 * 512; // 512MB
    stats_->current_pids = 5;
    stats_->pids_limit = 100;
    stats_->network_rx_bytes = 1024;
    stats_->network_tx_bytes = 2048;

    // Check values are set correctly
    EXPECT_EQ(stats_->cpu_usage_percent, 50.5);
    EXPECT_EQ(stats_->memory_usage_bytes, 1024 * 1024 * 100);
    EXPECT_EQ(stats_->memory_limit_bytes, 1024 * 1024 * 512);
    EXPECT_EQ(stats_->current_pids, 5);
    EXPECT_EQ(stats_->pids_limit, 100);
    EXPECT_EQ(stats_->network_rx_bytes, 1024);
    EXPECT_EQ(stats_->network_tx_bytes, 2048);
}
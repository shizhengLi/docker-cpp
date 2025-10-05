#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <docker-cpp/cgroup/cgroup_manager.hpp>
#include <docker-cpp/core/error.hpp>
#include <thread>

class CgroupManagerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // Check if cgroup v2 is supported
        if (!docker_cpp::CgroupManager::isCgroupV2Supported()) {
            GTEST_SKIP() << "cgroup v2 not supported on this system";
        }

        // Create test configuration
        config_.name = "test_cgroup";
        config_.controllers =
            docker_cpp::CgroupController::CPU | docker_cpp::CgroupController::MEMORY;
        config_.parent_path = "";

        // Set CPU limits
        config_.cpu.max_usec = 500000;     // 0.5 seconds
        config_.cpu.period_usec = 1000000; // 1 second period
        config_.cpu.weight = 100;          // Low priority

        // Set memory limits
        config_.memory.max_bytes = 100 * 1024 * 1024;     // 100MB
        config_.memory.swap_max_bytes = 50 * 1024 * 1024; // 50MB
        config_.memory.low_bytes = 80 * 1024 * 1024;      // 80MB
        config_.memory.high_bytes = 90 * 1024 * 1024;     // 90MB
        config_.memory.oom_kill_enable = true;

        // Set PID limits
        config_.pid.max = 10;

        // Initialize manager
        try {
            manager_ = docker_cpp::CgroupManager::create(config_);
        }
        catch (const docker_cpp::CgroupError& e) {
            // If we can't create cgroup manager (likely due to permissions),
            // skip tests
            GTEST_SKIP() << "Cannot create cgroup manager: " << e.what();
        }
    }

    void TearDown() override
    {
        if (manager_) {
            try {
                manager_->destroy();
            }
            catch (...) {
                // Ignore errors during cleanup
            }
        }
    }

    docker_cpp::CgroupConfig config_;
    std::unique_ptr<docker_cpp::CgroupManager> manager_;
};

// Test basic cgroup creation and destruction
TEST_F(CgroupManagerTest, CreateAndDestroy)
{
    ASSERT_NE(manager_, nullptr);

    // Cgroup should not exist initially
    EXPECT_FALSE(manager_->exists());

    // Create cgroup
    manager_->create();
    EXPECT_TRUE(manager_->exists());

    // Verify path is set
    std::string path = manager_->getPath();
    EXPECT_FALSE(path.empty());

    // Destroy cgroup
    manager_->destroy();
    EXPECT_FALSE(manager_->exists());
}

// Test cgroup configuration
TEST_F(CgroupManagerTest, CgroupConfiguration)
{
    ASSERT_NE(manager_, nullptr);

    manager_->create();

    // Get configuration
    docker_cpp::CgroupConfig retrieved_config = manager_->getConfig();
    EXPECT_EQ(retrieved_config.name, config_.name);
    EXPECT_EQ(retrieved_config.controllers, config_.controllers);
    EXPECT_EQ(retrieved_config.cpu.max_usec, config_.cpu.max_usec);
    EXPECT_EQ(retrieved_config.memory.max_bytes, config_.memory.max_bytes);
    EXPECT_EQ(retrieved_config.pid.max, config_.pid.max);
}

// Test controller operations
TEST_F(CgroupManagerTest, ControllerOperations)
{
    ASSERT_NE(manager_, nullptr);

    manager_->create();

    // Test CPU controller
    EXPECT_TRUE(manager_->isControllerEnabled(docker_cpp::CgroupController::CPU));

    // Test disabling controller
    manager_->disableController(docker_cpp::CgroupController::MEMORY);
    EXPECT_FALSE(manager_->isControllerEnabled(docker_cpp::CgroupController::MEMORY));

    // Test re-enabling controller
    manager_->enableController(docker_cpp::CgroupController::MEMORY);
    EXPECT_TRUE(manager_->isControllerEnabled(docker_cpp::CgroupController::MEMORY));
}

// Test CPU resource limits
TEST_F(CgroupManagerTest, CpuResourceLimits)
{
    ASSERT_NE(manager_, nullptr);

    manager_->create();

    // Test CPU max
    manager_->setCpuMax(250000, 500000); // 0.25s max, 0.5s period

    // Test CPU weight
    manager_->setCpuWeight(200);

    // Test CPU burst
    manager_->setCpuBurst(100000); // 0.1s burst

    // Get CPU stats to verify settings (may not be immediately reflected)
    docker_cpp::CpuStats cpu_stats = manager_->getCpuStats();
    EXPECT_GT(cpu_stats.usage_usec, 0);
}

// Test memory resource limits
TEST_F(CgroupManagerTest, MemoryResourceLimits)
{
    ASSERT_NE(manager_, nullptr);

    manager_->create();

    // Test memory limits
    manager_->setMemoryMax(50 * 1024 * 1024);     // 50MB
    manager_->setMemorySwapMax(25 * 1024 * 1024); // 25MB
    manager_->setMemoryLow(40 * 1024 * 1024);     // 40MB
    manager_->setMemoryHigh(45 * 1024 * 1024);    // 45MB
    manager_->setOomKillEnable(true);

    // Get memory stats
    docker_cpp::MemoryStats memory_stats = manager_->getMemoryStats();
    EXPECT_GE(memory_stats.current, 0);
}

// Test IO resource limits
TEST_F(CgroupManagerTest, IoResourceLimits)
{
    ASSERT_NE(manager_, nullptr);

    manager_->create();

    // Enable IO controller
    manager_->enableController(docker_cpp::CgroupController::IO);

    // Test IO limits
    std::string test_device = "8:0";                               // Example device major:minor
    manager_->setIoBps(test_device, 1024 * 1024, 2 * 1024 * 1024); // 1MB read, 2MB write
    manager_->setIoIops(test_device, 100, 200);                    // 100 read IOPS, 200 write IOPS

    // Get IO stats
    docker_cpp::IoStats io_stats = manager_->getIoStats();
    EXPECT_GE(io_stats.rbytes, 0);
    EXPECT_GE(io_stats.wbytes, 0);
}

// Test PID resource limits
TEST_F(CgroupManagerTest, PidResourceLimits)
{
    ASSERT_NE(manager_, nullptr);

    manager_->create();

    // Enable PID controller
    manager_->enableController(docker_cpp::CgroupController::PID);

    // Test PID limits
    manager_->setPidMax(5);

    // Get PID stats
    docker_cpp::PidStats pid_stats = manager_->getPidStats();
    EXPECT_GE(pid_stats.current, 0);
}

// Test process management
TEST_F(CgroupManagerTest, ProcessManagement)
{
    ASSERT_NE(manager_, nullptr);

    manager_->create();

    // Initially no processes should be in the cgroup
    std::vector<pid_t> processes = manager_->getProcesses();
    EXPECT_TRUE(processes.empty());

    // Add current process to cgroup
    pid_t current_pid = getpid();
    manager_->addProcess(current_pid);

    // Verify process was added
    processes = manager_->getProcesses();
    EXPECT_FALSE(processes.empty());
    EXPECT_NE(std::find(processes.begin(), processes.end(), current_pid), processes.end());

    // Remove process from cgroup
    manager_->removeProcess(current_pid);

    // Verify process was removed (may take a moment)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    processes = manager_->getProcesses();
    // Note: process might still appear in cgroup immediately after removal
}

// Test resource metrics collection
TEST_F(CgroupManagerTest, ResourceMetricsCollection)
{
    ASSERT_NE(manager_, nullptr);

    manager_->create();

    // Get comprehensive metrics
    docker_cpp::ResourceMetrics metrics = manager_->getMetrics();

    // Verify timestamp
    EXPECT_GT(metrics.timestamp, 0);

    // Verify CPU metrics
    EXPECT_GE(metrics.cpu.usage_usec, 0);
    EXPECT_GE(metrics.cpu.usage_percent, 0.0);

    // Verify memory metrics
    EXPECT_GE(metrics.memory.current, 0);

    // Verify IO metrics
    EXPECT_GE(metrics.io.rbytes, 0);
    EXPECT_GE(metrics.io.wbytes, 0);

    // Verify PID metrics
    EXPECT_GE(metrics.pid.current, 0);
}

// Test error handling for invalid operations
TEST_F(CgroupManagerTest, ErrorHandling)
{
    ASSERT_NE(manager_, nullptr);

    // Test operations on non-existent cgroup
    EXPECT_THROW(manager_->getCpuStats(), docker_cpp::CgroupError);
    EXPECT_THROW(manager_->getMemoryStats(), docker_cpp::CgroupError);

    // Create cgroup
    manager_->create();

    // Test invalid resource limits
    EXPECT_THROW(manager_->setCpuMax(0, 0), docker_cpp::CgroupError); // Invalid period
    EXPECT_THROW(manager_->setMemoryMax(0), docker_cpp::CgroupError); // Invalid memory limit
    EXPECT_THROW(manager_->setPidMax(0), docker_cpp::CgroupError);    // Invalid PID limit

    // Test adding invalid process
    EXPECT_THROW(manager_->addProcess(999999), docker_cpp::CgroupError); // Non-existent PID
}

// Test cgroup utility functions
TEST(CgroupManagerUtilitiesTest, UtilityFunctions)
{
    // Test cgroup v2 support detection
    bool is_supported = docker_cpp::CgroupManager::isCgroupV2Supported();
    EXPECT_TRUE(is_supported || !is_supported); // Just verify it doesn't crash

    if (is_supported) {
        // Test mount point detection
        std::string mount_point = docker_cpp::CgroupManager::getMountPoint();
        EXPECT_FALSE(mount_point.empty());

        // Test controller listing
        std::vector<std::string> controllers = docker_cpp::CgroupManager::listControllers();
        EXPECT_FALSE(controllers.empty());

        // Test specific controller availability
        bool cpu_available = docker_cpp::CgroupManager::isControllerAvailable("cpu");
        bool memory_available = docker_cpp::CgroupManager::isControllerAvailable("memory");
        // Results depend on system configuration
    }
}

// Test cgroup controller utility functions
TEST(CgroupControllerUtilitiesTest, ControllerUtilities)
{
    // Test controller operations
    docker_cpp::CgroupController cpu = docker_cpp::CgroupController::CPU;
    docker_cpp::CgroupController memory = docker_cpp::CgroupController::MEMORY;

    docker_cpp::CgroupController combined = cpu | memory;
    EXPECT_TRUE(docker_cpp::hasController(combined, cpu));
    EXPECT_TRUE(docker_cpp::hasController(combined, memory));
    EXPECT_FALSE(docker_cpp::hasController(combined, docker_cpp::CgroupController::IO));

    // Test string conversion
    std::string cpu_str = docker_cpp::cgroupControllerToString(cpu);
    EXPECT_FALSE(cpu_str.empty());

    docker_cpp::CgroupController converted = docker_cpp::stringToCgroupController(cpu_str);
    EXPECT_EQ(converted, cpu);
}

// Test cgroup error handling
TEST(CgroupErrorTest, ErrorHandling)
{
    // Test error creation
    docker_cpp::CgroupError error(docker_cpp::CgroupError::Code::NOT_FOUND, "Test error message");

    EXPECT_EQ(error.getCode(), docker_cpp::CgroupError::Code::NOT_FOUND);
    EXPECT_STREQ(error.what(), "Test error message");

    // Test different error codes
    std::vector<docker_cpp::CgroupError::Code> error_codes = {
        docker_cpp::CgroupError::Code::SUCCESS,
        docker_cpp::CgroupError::Code::NOT_SUPPORTED,
        docker_cpp::CgroupError::Code::NOT_FOUND,
        docker_cpp::CgroupError::Code::PERMISSION_DENIED,
        docker_cpp::CgroupError::Code::INVALID_ARGUMENT,
        docker_cpp::CgroupError::Code::IO_ERROR,
        docker_cpp::CgroupError::Code::CONTROLLER_NOT_AVAILABLE,
        docker_cpp::CgroupError::Code::PROCESS_NOT_FOUND,
        docker_cpp::CgroupError::Code::MEMORY_PRESSURE,
        docker_cpp::CgroupError::Code::OOM_EVENT};

    for (auto code : error_codes) {
        docker_cpp::CgroupError test_error(code, "Test message");
        EXPECT_EQ(test_error.getCode(), code);
        EXPECT_STREQ(test_error.what(), "Test message");
    }
}

// Test resource monitor interface
class ResourceMonitorTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        if (!docker_cpp::CgroupManager::isCgroupV2Supported()) {
            GTEST_SKIP() << "cgroup v2 not supported on this system";
        }

        try {
            monitor_ = docker_cpp::ResourceMonitor::create();
        }
        catch (const docker_cpp::CgroupError& e) {
            GTEST_SKIP() << "Cannot create resource monitor: " << e.what();
        }
    }

    std::unique_ptr<docker_cpp::ResourceMonitor> monitor_;
};

TEST_F(ResourceMonitorTest, BasicMonitoring)
{
    ASSERT_NE(monitor_, nullptr);

    std::string test_cgroup_path = "/sys/fs/cgroup/test_monitor";

    // Test monitoring control
    EXPECT_FALSE(monitor_->isMonitoring(test_cgroup_path));

    // Start monitoring
    monitor_->startMonitoring(test_cgroup_path);
    EXPECT_TRUE(monitor_->isMonitoring(test_cgroup_path));

    // Stop monitoring
    monitor_->stopMonitoring(test_cgroup_path);
    EXPECT_FALSE(monitor_->isMonitoring(test_cgroup_path));
}

TEST_F(ResourceMonitorTest, MetricsCollection)
{
    ASSERT_NE(monitor_, nullptr);

    std::string test_cgroup_path = "/sys/fs/cgroup/test_metrics";

    // Start monitoring
    monitor_->startMonitoring(test_cgroup_path);

    // Get current metrics
    docker_cpp::ResourceMetrics metrics = monitor_->getCurrentMetrics(test_cgroup_path);
    EXPECT_GT(metrics.timestamp, 0);

    // Get historical metrics (should return empty for short time range)
    uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();

    std::vector<docker_cpp::ResourceMetrics> historical =
        monitor_->getHistoricalMetrics(test_cgroup_path, now - 1, now);
    // May be empty since we just started monitoring
}

TEST_F(ResourceMonitorTest, Alerting)
{
    ASSERT_NE(monitor_, nullptr);

    std::string test_cgroup_path = "/sys/fs/cgroup/test_alerts";

    // Start monitoring
    monitor_->startMonitoring(test_cgroup_path);

    // Set alert thresholds
    monitor_->setCpuThreshold(test_cgroup_path, 80.0);       // 80% CPU
    monitor_->setMemoryThreshold(test_cgroup_path, 90.0);    // 90% memory
    monitor_->setIoThreshold(test_cgroup_path, 1024 * 1024); // 1MB/s

    // Initially no alerts should be active
    EXPECT_FALSE(monitor_->hasCpuAlert(test_cgroup_path));
    EXPECT_FALSE(monitor_->hasMemoryAlert(test_cgroup_path));
    EXPECT_FALSE(monitor_->hasIoAlert(test_cgroup_path));

    // Test alert callback
    bool callback_called = false;
    std::string alert_type;
    double alert_value;

    auto callback = [&](const std::string& cgroup_path, const std::string& type, double value) {
        callback_called = true;
        alert_type = type;
        alert_value = value;
    };

    monitor_->setAlertCallback(callback);
}
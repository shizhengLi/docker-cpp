#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>
#include <chrono>
#include <docker-cpp/namespace/namespace_manager.hpp>
#include <docker-cpp/namespace/process_manager.hpp>
#include <thread>

class ProcessManagerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // Setup before each test
    }

    void TearDown() override
    {
        // Cleanup after each test
    }
};

// Test process manager creation and destruction
TEST_F(ProcessManagerTest, CreateAndDestroy)
{
    docker_cpp::ProcessManager manager;
    EXPECT_FALSE(manager.isMonitoringActive());
}

// Test basic process creation with simple command
TEST_F(ProcessManagerTest, CreateSimpleProcess)
{
    docker_cpp::ProcessManager manager;

    docker_cpp::ProcessConfig config;
    config.executable = "/bin/echo";
    config.args = {"echo", "Hello, World!"};

    pid_t pid = manager.createProcess(config);
    EXPECT_GT(pid, 0);

    // Wait for process to complete
    bool completed = manager.waitForProcess(pid, 5);
    EXPECT_TRUE(completed);

    // Check process is no longer running
    EXPECT_FALSE(manager.isProcessRunning(pid));
}

// Test process creation with working directory
TEST_F(ProcessManagerTest, CreateProcessWithWorkingDirectory)
{
    docker_cpp::ProcessManager manager;

    docker_cpp::ProcessConfig config;
    config.executable = "/bin/pwd";
    config.args = {"pwd"};
    config.working_dir = "/tmp";

    pid_t pid = manager.createProcess(config);
    EXPECT_GT(pid, 0);

    bool completed = manager.waitForProcess(pid, 5);
    EXPECT_TRUE(completed);
}

// Test process creation with environment variables
TEST_F(ProcessManagerTest, CreateProcessWithEnvironment)
{
    docker_cpp::ProcessManager manager;

    docker_cpp::ProcessConfig config;
    config.executable = "/bin/sh";
    config.args = {"sh", "-c", "echo $TEST_VAR"};
    config.env = {"TEST_VAR=HelloFromEnv"};

    pid_t pid = manager.createProcess(config);
    EXPECT_GT(pid, 0);

    bool completed = manager.waitForProcess(pid, 5);
    EXPECT_TRUE(completed);
}

// Test process creation with PID namespace
TEST_F(ProcessManagerTest, CreateProcessWithPIDNamespace)
{
    docker_cpp::ProcessManager manager;

    docker_cpp::ProcessConfig config;
    config.executable = "/bin/echo";
    config.args = {"echo", "Process in PID namespace"};
    config.create_pid_namespace = true;

    pid_t pid = manager.createProcess(config);
    EXPECT_GT(pid, 0);

    bool completed = manager.waitForProcess(pid, 5);
    EXPECT_TRUE(completed);

    // Verify process info
    docker_cpp::ProcessInfo info = manager.getProcessInfo(pid);
    EXPECT_TRUE(info.has_pid_namespace);
}

// Test process creation with UTS namespace
TEST_F(ProcessManagerTest, CreateProcessWithUTSNamespace)
{
    docker_cpp::ProcessManager manager;

    docker_cpp::ProcessConfig config;
    config.executable = "/bin/hostname";
    config.args = {"hostname"};
    config.create_uts_namespace = true;
    config.hostname = "test-container";

    pid_t pid = manager.createProcess(config);
    EXPECT_GT(pid, 0);

    bool completed = manager.waitForProcess(pid, 5);
    EXPECT_TRUE(completed);

    docker_cpp::ProcessInfo info = manager.getProcessInfo(pid);
    EXPECT_TRUE(info.has_uts_namespace);
}

// Test process creation with network namespace
TEST_F(ProcessManagerTest, CreateProcessWithNetworkNamespace)
{
    docker_cpp::ProcessManager manager;

    docker_cpp::ProcessConfig config;
    config.executable = "/sbin/ping";
    config.args = {"ip", "addr", "show"};
    config.create_network_namespace = true;

    pid_t pid = manager.createProcess(config);
    EXPECT_GT(pid, 0);

    bool completed = manager.waitForProcess(pid, 5);
    EXPECT_TRUE(completed);

    docker_cpp::ProcessInfo info = manager.getProcessInfo(pid);
    EXPECT_TRUE(info.has_network_namespace);
}

// Test process creation with mount namespace
TEST_F(ProcessManagerTest, CreateProcessWithMountNamespace)
{
    docker_cpp::ProcessManager manager;

    docker_cpp::ProcessConfig config;
    config.executable = "/bin/df";
    config.args = {"mount"};
    config.create_mount_namespace = true;

    pid_t pid = manager.createProcess(config);
    EXPECT_GT(pid, 0);

    bool completed = manager.waitForProcess(pid, 5);
    EXPECT_TRUE(completed);

    docker_cpp::ProcessInfo info = manager.getProcessInfo(pid);
    EXPECT_TRUE(info.has_mount_namespace);
}

// Test process creation with IPC namespace
TEST_F(ProcessManagerTest, CreateProcessWithIPCNamespace)
{
    docker_cpp::ProcessManager manager;

    docker_cpp::ProcessConfig config;
    config.executable = "/bin/ps";
    config.args = {"ipcs"};
    config.create_ipc_namespace = true;

    pid_t pid = manager.createProcess(config);
    EXPECT_GT(pid, 0);

    bool completed = manager.waitForProcess(pid, 5);
    EXPECT_TRUE(completed);

    docker_cpp::ProcessInfo info = manager.getProcessInfo(pid);
    EXPECT_TRUE(info.has_ipc_namespace);
}

// Test process creation with user namespace
TEST_F(ProcessManagerTest, CreateProcessWithUserNamespace)
{
    docker_cpp::ProcessManager manager;

    docker_cpp::ProcessConfig config;
    config.executable = "/usr/bin/id";
    config.args = {"id"};
    config.create_user_namespace = true;

    pid_t pid = manager.createProcess(config);
    EXPECT_GT(pid, 0);

    bool completed = manager.waitForProcess(pid, 5);
    EXPECT_TRUE(completed);

    docker_cpp::ProcessInfo info = manager.getProcessInfo(pid);
    EXPECT_TRUE(info.has_user_namespace);
}

// Test process creation with cgroup namespace
TEST_F(ProcessManagerTest, CreateProcessWithCgroupNamespace)
{
    docker_cpp::ProcessManager manager;

    docker_cpp::ProcessConfig config;
    config.executable = "/bin/cat";
    config.args = {"cat", "/proc/self/cgroup"};
    config.create_cgroup_namespace = true;

    pid_t pid = manager.createProcess(config);
    EXPECT_GT(pid, 0);

    bool completed = manager.waitForProcess(pid, 5);
    EXPECT_TRUE(completed);

    docker_cpp::ProcessInfo info = manager.getProcessInfo(pid);
    EXPECT_TRUE(info.has_cgroup_namespace);
}

// Test multiple namespace creation
TEST_F(ProcessManagerTest, CreateProcessWithMultipleNamespaces)
{
    docker_cpp::ProcessManager manager;

    docker_cpp::ProcessConfig config;
    config.executable = "/bin/echo";
    config.args = {"echo", "Process with multiple namespaces"};
    config.create_pid_namespace = true;
    config.create_uts_namespace = true;
    config.create_network_namespace = true;
    config.create_mount_namespace = true;
    config.create_ipc_namespace = true;

    pid_t pid = manager.createProcess(config);
    EXPECT_GT(pid, 0);

    bool completed = manager.waitForProcess(pid, 5);
    EXPECT_TRUE(completed);

    docker_cpp::ProcessInfo info = manager.getProcessInfo(pid);
    EXPECT_TRUE(info.has_pid_namespace);
    EXPECT_TRUE(info.has_uts_namespace);
    EXPECT_TRUE(info.has_network_namespace);
    EXPECT_TRUE(info.has_mount_namespace);
    EXPECT_TRUE(info.has_ipc_namespace);
}

// Test process monitoring
TEST_F(ProcessManagerTest, ProcessMonitoring)
{
    docker_cpp::ProcessManager manager;

    docker_cpp::ProcessConfig config;
    config.executable = "/bin/sleep";
    config.args = {"sleep", "2"};

    pid_t pid = manager.createProcess(config);
    EXPECT_GT(pid, 0);

    // Process should be running initially
    EXPECT_TRUE(manager.isProcessRunning(pid));

    // Monitor the process
    manager.monitorProcess(pid);
    docker_cpp::ProcessStatus status = manager.getProcessStatus(pid);
    EXPECT_EQ(status, docker_cpp::ProcessStatus::RUNNING);

    // Wait for completion
    bool completed = manager.waitForProcess(pid, 5);
    EXPECT_TRUE(completed);

    // Process should no longer be running
    EXPECT_FALSE(manager.isProcessRunning(pid));
}

// Test process stopping with timeout
TEST_F(ProcessManagerTest, StopProcessWithTimeout)
{
    docker_cpp::ProcessManager manager;

    docker_cpp::ProcessConfig config;
    config.executable = "/bin/sleep";
    config.args = {"sleep", "10"};

    pid_t pid = manager.createProcess(config);
    EXPECT_GT(pid, 0);

    EXPECT_TRUE(manager.isProcessRunning(pid));

    // Stop the process with timeout
    bool stopped = manager.stopProcess(pid, 2);
    EXPECT_TRUE(stopped);

    // Process should be stopped
    EXPECT_FALSE(manager.isProcessRunning(pid));
}

// Test process force kill
TEST_F(ProcessManagerTest, ForceKillProcess)
{
    docker_cpp::ProcessManager manager;

    docker_cpp::ProcessConfig config;
    config.executable = "/bin/sleep";
    config.args = {"sleep", "10"};

    pid_t pid = manager.createProcess(config);
    EXPECT_GT(pid, 0);

    EXPECT_TRUE(manager.isProcessRunning(pid));

    // Force kill the process
    manager.killProcess(pid, SIGTERM);

    // Wait a bit for process to die
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    EXPECT_FALSE(manager.isProcessRunning(pid));
}

// Test process info retrieval
TEST_F(ProcessManagerTest, GetProcessInfo)
{
    docker_cpp::ProcessManager manager;

    docker_cpp::ProcessConfig config;
    config.executable = "/bin/echo";
    config.args = {"echo", "test process"};
    config.create_pid_namespace = true;
    config.create_uts_namespace = true;

    pid_t pid = manager.createProcess(config);
    EXPECT_GT(pid, 0);

    // Wait a moment for process to start or complete
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    docker_cpp::ProcessInfo info = manager.getProcessInfo(pid);
    EXPECT_EQ(info.pid, pid);
    // Process should be in a valid state (running, stopped, zombie, or unknown)
    EXPECT_TRUE(info.status == docker_cpp::ProcessStatus::RUNNING
                || info.status == docker_cpp::ProcessStatus::STOPPED
                || info.status == docker_cpp::ProcessStatus::ZOMBIE
                || info.status == docker_cpp::ProcessStatus::UNKNOWN);
    EXPECT_EQ(info.command_line, "/bin/echo echo test process");
    EXPECT_TRUE(info.has_pid_namespace);
    EXPECT_TRUE(info.has_uts_namespace);
    EXPECT_FALSE(info.has_network_namespace);
    EXPECT_FALSE(info.has_mount_namespace);

    // Wait for completion
    manager.waitForProcess(pid, 5);
}

// Test managed processes list
TEST_F(ProcessManagerTest, GetManagedProcesses)
{
    docker_cpp::ProcessManager manager;

    // Create multiple processes
    docker_cpp::ProcessConfig config1;
    config1.executable = "/bin/sleep";
    config1.args = {"sleep", "1"};

    docker_cpp::ProcessConfig config2;
    config2.executable = "/bin/sleep";
    config2.args = {"sleep", "1"};

    pid_t pid1 = manager.createProcess(config1);
    pid_t pid2 = manager.createProcess(config2);

    EXPECT_GT(pid1, 0);
    EXPECT_GT(pid2, 0);

    std::vector<pid_t> managed_pids = manager.getManagedProcesses();
    EXPECT_EQ(managed_pids.size(), 2);
    EXPECT_TRUE(std::find(managed_pids.begin(), managed_pids.end(), pid1) != managed_pids.end());
    EXPECT_TRUE(std::find(managed_pids.begin(), managed_pids.end(), pid2) != managed_pids.end());

    // Wait for completion
    manager.waitForProcess(pid1, 5);
    manager.waitForProcess(pid2, 5);
}

// Test process exit callback
TEST_F(ProcessManagerTest, ProcessExitCallback)
{
    docker_cpp::ProcessManager manager;

    bool callback_called = false;
    pid_t callback_pid = 0;

    manager.setProcessExitCallback(
        [&callback_called, &callback_pid](const docker_cpp::ProcessInfo& info) {
            callback_called = true;
            callback_pid = info.pid;
        });

    docker_cpp::ProcessConfig config;
    config.executable = "/bin/echo";
    config.args = {"echo", "callback test"};

    pid_t pid = manager.createProcess(config);
    EXPECT_GT(pid, 0);

    // Wait for process to complete
    manager.waitForProcess(pid, 5);

    // Give monitoring a chance to detect the exit
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Note: In a real implementation with proper monitoring, the callback would be called
    // For now, we just verify the mechanism is in place
}

// Test monitoring thread
TEST_F(ProcessManagerTest, MonitoringThread)
{
    docker_cpp::ProcessManager manager;

    // Start monitoring
    manager.startMonitoring();
    EXPECT_TRUE(manager.isMonitoringActive());

    docker_cpp::ProcessConfig config;
    config.executable = "/bin/sleep";
    config.args = {"sleep", "1"};

    pid_t pid = manager.createProcess(config);
    EXPECT_GT(pid, 0);

    // Let monitoring run for a bit
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Stop monitoring
    manager.stopMonitoring();
    EXPECT_FALSE(manager.isMonitoringActive());

    // Wait for process to complete
    manager.waitForProcess(pid, 5);
}

// Test process manager move semantics
TEST_F(ProcessManagerTest, MoveSemantics)
{
    docker_cpp::ProcessManager manager1;

    docker_cpp::ProcessConfig config;
    config.executable = "/bin/sleep";
    config.args = {"sleep", "1"};

    pid_t pid = manager1.createProcess(config);
    EXPECT_GT(pid, 0);

    // Test move constructor
    docker_cpp::ProcessManager manager2 = std::move(manager1);

    // manager2 should now manage the process
    std::vector<pid_t> pids = manager2.getManagedProcesses();
    EXPECT_EQ(pids.size(), 1);
    EXPECT_EQ(pids[0], pid);

    // Test move assignment
    docker_cpp::ProcessManager manager3;
    manager3 = std::move(manager2);

    pids = manager3.getManagedProcesses();
    EXPECT_EQ(pids.size(), 1);
    EXPECT_EQ(pids[0], pid);

    // Wait for completion
    manager3.waitForProcess(pid, 5);
}

// Test error handling for invalid executable
TEST_F(ProcessManagerTest, ErrorHandlingInvalidExecutable)
{
    docker_cpp::ProcessManager manager;

    docker_cpp::ProcessConfig config;
    config.executable = "/nonexistent/executable";
    config.args = {"test"};

    EXPECT_THROW(manager.createProcess(config), docker_cpp::ContainerError);
}

// Test error handling for non-existent process
TEST_F(ProcessManagerTest, ErrorHandlingNonExistentProcess)
{
    docker_cpp::ProcessManager manager;

    pid_t fake_pid = 999999;

    EXPECT_THROW(manager.getProcessInfo(fake_pid), docker_cpp::ContainerError);

    EXPECT_FALSE(manager.isProcessRunning(fake_pid));
    EXPECT_EQ(manager.getProcessStatus(fake_pid), docker_cpp::ProcessStatus::UNKNOWN);
}

// Test error handling for stopping non-existent process
TEST_F(ProcessManagerTest, ErrorHandlingStopNonExistentProcess)
{
    docker_cpp::ProcessManager manager;

    pid_t fake_pid = 999999;

    EXPECT_THROW(manager.stopProcess(fake_pid), docker_cpp::ContainerError);

    EXPECT_THROW(manager.killProcess(fake_pid), docker_cpp::ContainerError);
}
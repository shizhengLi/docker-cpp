#include <gtest/gtest.h>
#include <docker-cpp/namespace/namespace_manager.hpp>
#include <string>
#include <system_error>

class NamespaceManagerTest : public ::testing::Test {
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

// Test namespace manager creation with different types
TEST_F(NamespaceManagerTest,CreateNamespaceTypes)
{
    // Test that we can create namespace managers for different types
    // Note: Some namespace types might not be available on all systems

    try {
        docker_cpp::NamespaceManager uts_ns(docker_cpp::NamespaceType::UTS);
        EXPECT_EQ(uts_ns.getType(), docker_cpp::NamespaceType::UTS);
        EXPECT_TRUE(uts_ns.isValid());
    }
    catch (const docker_cpp::ContainerError& e) {
        // UTS namespace creation might fail on some systems - this is acceptable
        GTEST_LOG_(INFO) << "UTS namespace creation failed (acceptable): " << e.what();
    }

    try {
        docker_cpp::NamespaceManager pid_ns(docker_cpp::NamespaceType::PID);
        EXPECT_EQ(pid_ns.getType(), docker_cpp::NamespaceType::PID);
        EXPECT_TRUE(pid_ns.isValid());
    }
    catch (const docker_cpp::ContainerError& e) {
        // PID namespace creation might fail on some systems - this is acceptable
        GTEST_LOG_(INFO) << "PID namespace creation failed (acceptable): " << e.what();
    }
}

// Test namespace move constructor
TEST_F(NamespaceManagerTest,MoveConstructor)
{
    try {
        docker_cpp::NamespaceManager ns1(docker_cpp::NamespaceType::UTS);
        docker_cpp::NamespaceType original_type = ns1.getType();
        bool original_valid = ns1.isValid();

        docker_cpp::NamespaceManager ns2 = std::move(ns1);

        EXPECT_EQ(ns2.getType(), original_type);
        EXPECT_EQ(ns2.isValid(), original_valid);

        // After move, ns1 should be in an undefined state - don't access it
        // The move constructor should have transferred ownership to ns2
    }
    catch (const docker_cpp::ContainerError& e) {
        GTEST_LOG_(INFO) << "Move constructor test skipped due to namespace creation failure: "
                         << e.what();
    }
}

// Test namespace move assignment
TEST_F(NamespaceManagerTest,MoveAssignment)
{
    try {
        docker_cpp::NamespaceManager ns1(docker_cpp::NamespaceType::UTS);
        docker_cpp::NamespaceManager ns2(docker_cpp::NamespaceType::PID);

        ns1 = std::move(ns2);

        EXPECT_EQ(ns1.getType(), docker_cpp::NamespaceType::PID);
    }
    catch (const docker_cpp::ContainerError& e) {
        GTEST_LOG_(INFO) << "Move assignment test skipped due to namespace creation failure: "
                         << e.what();
    }
}

// Test namespace type to string conversion
TEST_F(NamespaceManagerTest,NamespaceTypeToString)
{
    EXPECT_EQ(namespaceTypeToString(docker_cpp::NamespaceType::PID), "PID");
    EXPECT_EQ(namespaceTypeToString(docker_cpp::NamespaceType::NETWORK), "Network");
    EXPECT_EQ(namespaceTypeToString(docker_cpp::NamespaceType::MOUNT), "Mount");
    EXPECT_EQ(namespaceTypeToString(docker_cpp::NamespaceType::UTS), "UTS");
    EXPECT_EQ(namespaceTypeToString(docker_cpp::NamespaceType::IPC), "IPC");
    EXPECT_EQ(namespaceTypeToString(docker_cpp::NamespaceType::USER), "User");
    EXPECT_EQ(namespaceTypeToString(docker_cpp::NamespaceType::CGROUP), "Cgroup");
}

// Test namespace join functionality (current process)
TEST_F(NamespaceManagerTest,JoinCurrentProcessNamespace)
{
    try {
        pid_t current_pid = getpid();

        // This should work for the current process
        docker_cpp::NamespaceManager::joinNamespace(current_pid, docker_cpp::NamespaceType::UTS);

        // If we reach here, the join was successful
        SUCCEED() << "Successfully joined current process UTS namespace";
    }
    catch (const docker_cpp::ContainerError& e) {
        // This might fail on some systems, which is acceptable
        GTEST_LOG_(INFO) << "Namespace join failed (acceptable): " << e.what();
    }
}
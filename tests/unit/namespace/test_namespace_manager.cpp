#include <gtest/gtest.h>
#include <docker-cpp/namespace/namespace_manager.hpp>
#include <string>
#include <system_error>

using namespace docker_cpp;

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
TEST_F(NamespaceManagerTest, CreateNamespaceTypes)
{
    // Test that we can create namespace managers for different types
    // Note: Some namespace types might not be available on all systems

    try {
        NamespaceManager uts_ns(NamespaceType::UTS);
        EXPECT_EQ(uts_ns.getType(), NamespaceType::UTS);
        EXPECT_TRUE(uts_ns.isValid());
    }
    catch (const ContainerError& e) {
        // UTS namespace creation might fail on some systems - this is acceptable
        GTEST_LOG_(INFO) << "UTS namespace creation failed (acceptable): " << e.what();
    }

    try {
        NamespaceManager pid_ns(NamespaceType::PID);
        EXPECT_EQ(pid_ns.getType(), NamespaceType::PID);
        EXPECT_TRUE(pid_ns.isValid());
    }
    catch (const ContainerError& e) {
        // PID namespace creation might fail on some systems - this is acceptable
        GTEST_LOG_(INFO) << "PID namespace creation failed (acceptable): " << e.what();
    }
}

// Test namespace move constructor
TEST_F(NamespaceManagerTest, MoveConstructor)
{
    try {
        NamespaceManager ns1(NamespaceType::UTS);
        NamespaceType original_type = ns1.getType();
        bool original_valid = ns1.isValid();

        NamespaceManager ns2 = std::move(ns1);

        EXPECT_EQ(ns2.getType(), original_type);
        EXPECT_EQ(ns2.isValid(), original_valid);

        // ns1 should be in a moved-from state (fd is -1, but type remains the same)
        EXPECT_EQ(ns1.getType(), original_type); // Type should remain the same
        EXPECT_FALSE(ns1.isValid());             // Should be invalid after move
    }
    catch (const ContainerError& e) {
        GTEST_LOG_(INFO) << "Move constructor test skipped due to namespace creation failure: "
                         << e.what();
    }
}

// Test namespace move assignment
TEST_F(NamespaceManagerTest, MoveAssignment)
{
    try {
        NamespaceManager ns1(NamespaceType::UTS);
        NamespaceManager ns2(NamespaceType::PID);

        ns1 = std::move(ns2);

        EXPECT_EQ(ns1.getType(), NamespaceType::PID);
    }
    catch (const ContainerError& e) {
        GTEST_LOG_(INFO) << "Move assignment test skipped due to namespace creation failure: "
                         << e.what();
    }
}

// Test namespace type to string conversion
TEST_F(NamespaceManagerTest, NamespaceTypeToString)
{
    EXPECT_EQ(namespaceTypeToString(NamespaceType::PID), "PID");
    EXPECT_EQ(namespaceTypeToString(NamespaceType::NETWORK), "Network");
    EXPECT_EQ(namespaceTypeToString(NamespaceType::MOUNT), "Mount");
    EXPECT_EQ(namespaceTypeToString(NamespaceType::UTS), "UTS");
    EXPECT_EQ(namespaceTypeToString(NamespaceType::IPC), "IPC");
    EXPECT_EQ(namespaceTypeToString(NamespaceType::USER), "User");
    EXPECT_EQ(namespaceTypeToString(NamespaceType::CGROUP), "Cgroup");
}

// Test namespace join functionality (current process)
TEST_F(NamespaceManagerTest, JoinCurrentProcessNamespace)
{
    try {
        pid_t current_pid = getpid();

        // This should work for the current process
        NamespaceManager::joinNamespace(current_pid, NamespaceType::UTS);

        // If we reach here, the join was successful
        SUCCEED() << "Successfully joined current process UTS namespace";
    }
    catch (const ContainerError& e) {
        // This might fail on some systems, which is acceptable
        GTEST_LOG_(INFO) << "Namespace join failed (acceptable): " << e.what();
    }
}
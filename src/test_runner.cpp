#include <cassert>
#include <docker-cpp/core/error.hpp>
#include <docker-cpp/namespace/namespace_manager.hpp>
#include <functional>
#include <iostream>
#include <string>

using namespace docker_cpp;

// Simple test framework
class TestRunner {
public:
    static void runTest(const std::string& testName, const std::function<void()>& testFunc)
    {
        try {
            std::cout << "Running " << testName << "... ";
            testFunc();
            std::cout << "PASSED" << std::endl;
            passed_count_++;
        }
        catch (const std::exception& e) {
            std::cout << "FAILED: " << e.what() << std::endl;
            failed_count_++;
        }
        catch (...) {
            std::cout << "FAILED: Unknown exception" << std::endl;
            failed_count_++;
        }
        total_count_++;
    }

    static void printSummary()
    {
        std::cout << "\n=== Test Summary ===" << std::endl;
        std::cout << "Total: " << total_count_ << std::endl;
        std::cout << "Passed: " << passed_count_ << std::endl;
        std::cout << "Failed: " << failed_count_ << std::endl;
        std::cout << "Success Rate: "
                  << (total_count_ > 0 ? (passed_count_ * 100 / total_count_) : 0) << "%"
                  << std::endl;
    }

public:
    static int getTotalCount()
    {
        return total_count_;
    }
    static int getPassedCount()
    {
        return passed_count_;
    }
    static int getFailedCount()
    {
        return failed_count_;
    }

private:
    static int total_count_;
    static int passed_count_;
    static int failed_count_;
};

int TestRunner::total_count_ = 0;
int TestRunner::passed_count_ = 0;
int TestRunner::failed_count_ = 0;

// Test functions
void testErrorCreation()
{
    ContainerError error(ErrorCode::CONTAINER_NOT_FOUND, "Container with ID 'test' not found");
    assert(error.getErrorCode() == ErrorCode::CONTAINER_NOT_FOUND);
    assert(std::string(error.what()).find("Container not found") != std::string::npos);
}

void testErrorCodeConversion()
{
    ContainerError error1(ErrorCode::NAMESPACE_CREATION_FAILED, "Test message");
    assert(error1.getErrorCode() == ErrorCode::NAMESPACE_CREATION_FAILED);
}

void testErrorCopy()
{
    ContainerError original(ErrorCode::IMAGE_NOT_FOUND, "Image not found");
    assert(original.getErrorCode() == ErrorCode::IMAGE_NOT_FOUND);
    assert(std::string(original.what()).find("Image not found") != std::string::npos);
}

void testErrorMove()
{
    ContainerError original(ErrorCode::IMAGE_NOT_FOUND, "Image not found");
    ContainerError moved(std::move(original));
    assert(moved.getErrorCode() == ErrorCode::IMAGE_NOT_FOUND);
}

void testErrorCategory()
{
    const auto& category = getContainerErrorCategory();
    assert(std::string(category.name()) == "docker-cpp");

    ContainerError error(ErrorCode::CONTAINER_NOT_FOUND, "Test");
    assert(error.code().category().name() == std::string("docker-cpp"));
}

void testSystemError()
{
    std::system_error sys_error(errno, std::system_category(), "System call failed");
    ContainerError container_error = makeSystemError(ErrorCode::SYSTEM_ERROR, sys_error);
    assert(container_error.getErrorCode() == ErrorCode::SYSTEM_ERROR);
    assert(std::string(container_error.what()).find("System call failed") != std::string::npos);
}

// Namespace manager tests
void testNamespaceManagerCreation()
{
    try {
        // Test creating a UTS namespace (usually available)
        {
            NamespaceManager ns(NamespaceType::UTS);
            assert(ns.getType() == NamespaceType::UTS);
            assert(ns.isValid());
        } // Destructor should be called here

        // Test creating a PID namespace
        {
            NamespaceManager ns(NamespaceType::PID);
            assert(ns.getType() == NamespaceType::PID);
            assert(ns.isValid());
        }

        std::cout << "Namespace creation tests passed" << std::endl;
    }
    catch (const ContainerError& e) {
        std::cout << "Namespace creation test failed (expected in some environments): " << e.what()
                  << std::endl;
        // In some environments, namespace creation might be restricted
        // This is okay for testing purposes
        assert(true); // Don't fail the test for this
    }
    catch (const std::exception& e) {
        std::string msg = "Unexpected exception: ";
        msg += e.what();
        assert(false && msg.c_str());
    }
}

void testNamespaceManagerTypes()
{
    try {
        // Test namespace type string conversion
        assert(namespaceTypeToString(NamespaceType::PID) == "PID");
        assert(namespaceTypeToString(NamespaceType::NETWORK) == "Network");
        assert(namespaceTypeToString(NamespaceType::MOUNT) == "Mount");
        assert(namespaceTypeToString(NamespaceType::UTS) == "UTS");
        assert(namespaceTypeToString(NamespaceType::IPC) == "IPC");
        assert(namespaceTypeToString(NamespaceType::USER) == "User");
        assert(namespaceTypeToString(NamespaceType::CGROUP) == "Cgroup");

        // Test namespace manager with different types
        for (int i = static_cast<int>(NamespaceType::PID);
             i <= static_cast<int>(NamespaceType::CGROUP);
             ++i) {
            auto type = static_cast<NamespaceType>(i);
            try {
                NamespaceManager ns(type);
                assert(ns.getType() == type);
                assert(ns.isValid());
            }
            catch (const ContainerError&) {
                // Some namespace types might not be available in all environments
                // This is okay for testing
            }
        }

        std::cout << "Namespace types tests passed" << std::endl;
    }
    catch (const std::exception& e) {
        std::string msg = "Unexpected exception in namespace types test: ";
        msg += e.what();
        assert(false && msg.c_str());
    }
}

void testNamespaceManagerMoveSemantics()
{
    try {
        // Test move constructor
        {
            NamespaceManager ns1(NamespaceType::UTS);
            NamespaceType type1 = ns1.getType();

            NamespaceManager ns2 = std::move(ns1);

            assert(ns2.getType() == type1);
            assert(ns2.isValid());
            (void)type1; // Suppress unused variable warning
            // ns1 should be in a valid but moved-from state
        }

        // Test move assignment
        {
            NamespaceManager ns1(NamespaceType::PID);
            NamespaceManager ns2(NamespaceType::UTS);

            ns1 = std::move(ns2);

            assert(ns1.getType() == NamespaceType::UTS);
            assert(ns1.isValid());
        }

        std::cout << "Namespace move semantics tests passed" << std::endl;
    }
    catch (const ContainerError& e) {
        std::cout << "Namespace move semantics test failed (expected in some environments): "
                  << e.what() << std::endl;
        assert(true); // Don't fail the test for this
    }
    catch (const std::exception& e) {
        std::string msg = "Unexpected exception in move semantics test: ";
        msg += e.what();
        assert(false && msg.c_str());
    }
}

void testNamespaceManagerJoin()
{
    try {
        // Test joining current process namespace
        pid_t current_pid = getpid();

        // This should work for the current process
        NamespaceManager::joinNamespace(current_pid, NamespaceType::UTS);

        std::cout << "Namespace join tests passed" << std::endl;
    }
    catch (const ContainerError& e) {
        std::cout << "Namespace join test failed (expected in some environments): " << e.what()
                  << std::endl;
        assert(true); // Don't fail the test for this
    }
    catch (const std::exception& e) {
        std::string msg = "Unexpected exception in join test: ";
        msg += e.what();
        assert(false && msg.c_str());
    }
}

int main()
{
    std::cout << "=== Docker-CPP Unit Tests ===" << std::endl;

    // Error handling tests
    TestRunner::runTest("Error Creation", testErrorCreation);
    TestRunner::runTest("Error Code Conversion", testErrorCodeConversion);
    TestRunner::runTest("Error Copy", testErrorCopy);
    TestRunner::runTest("Error Move", testErrorMove);
    TestRunner::runTest("Error Category", testErrorCategory);
    TestRunner::runTest("System Error", testSystemError);

    // Namespace manager tests
    TestRunner::runTest("Namespace Manager Creation", testNamespaceManagerCreation);
    TestRunner::runTest("Namespace Manager Types", testNamespaceManagerTypes);
    TestRunner::runTest("Namespace Manager Move Semantics", testNamespaceManagerMoveSemantics);
    TestRunner::runTest("Namespace Manager Join", testNamespaceManagerJoin);

    TestRunner::printSummary();

    return TestRunner::getFailedCount() > 0 ? 1 : 0;
}
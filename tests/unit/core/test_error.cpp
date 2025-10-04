#include <gtest/gtest.h>
#include <docker-cpp/core/error.hpp>
#include <system_error>
#include <string>
#include <unordered_set>

using namespace docker_cpp;

class ErrorTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(ErrorTest, BasicErrorCreation) {
    // Test basic error creation
    ContainerError error(ErrorCode::CONTAINER_NOT_FOUND, "Container with ID 'test' not found");

    EXPECT_EQ(error.getErrorCode(), ErrorCode::CONTAINER_NOT_FOUND);
    EXPECT_STREQ(error.what(), "[docker-cpp 1000] Container not found: Container with ID 'test' not found");
}

TEST_F(ErrorTest, ErrorCodeConversion) {
    // Test error code to string conversion
    ContainerError error1(ErrorCode::CONTAINER_NOT_FOUND, "Test message");
    EXPECT_EQ(error1.getErrorCode(), ErrorCode::CONTAINER_NOT_FOUND);

    ContainerError error2(ErrorCode::NAMESPACE_CREATION_FAILED, "Namespace creation failed");
    EXPECT_EQ(error2.getErrorCode(), ErrorCode::NAMESPACE_CREATION_FAILED);
}

TEST_F(ErrorTest, ErrorCopyAndMove) {
    // Test error copy constructor
    ContainerError original(ErrorCode::IMAGE_NOT_FOUND, "Image not found");
    ContainerError copied(original);

    EXPECT_EQ(copied.getErrorCode(), ErrorCode::IMAGE_NOT_FOUND);
    EXPECT_STREQ(copied.what(), "[docker-cpp 2000] Image not found: Image not found");

    // Test error move constructor
    ContainerError moved(std::move(original));
    EXPECT_EQ(moved.getErrorCode(), ErrorCode::IMAGE_NOT_FOUND);
}

TEST_F(ErrorTest, ErrorWithSystemError) {
    // Test error wrapping system errors
    std::system_error sys_error(errno, std::system_category(), "System call failed");
    ContainerError container_error = makeSystemError(ErrorCode::SYSTEM_ERROR, sys_error);

    EXPECT_EQ(container_error.getErrorCode(), ErrorCode::SYSTEM_ERROR);
    EXPECT_TRUE(std::string(container_error.what()).find("System call failed") != std::string::npos);
}

TEST_F(ErrorTest, ErrorCategory) {
    // Test custom error category
    const auto& category = getContainerErrorCategory();
    EXPECT_STREQ(category.name(), "docker-cpp");

    ContainerError error(ErrorCode::CONTAINER_NOT_FOUND, "Test");
    EXPECT_EQ(error.code().category(), category);
}

TEST_F(ErrorTest, ErrorCodeValues) {
    // Test that error codes have unique values
    std::unordered_set<int> error_codes;

    for (int i = static_cast<int>(ErrorCode::CONTAINER_NOT_FOUND);
         i <= static_cast<int>(ErrorCode::UNKNOWN_ERROR);
         ++i) {
        EXPECT_TRUE(error_codes.insert(i).second) << "Duplicate error code detected: " << i;
    }
}

TEST_F(ErrorTest, ErrorMessageFormatting) {
    // Test message formatting with custom message
    ContainerError error(ErrorCode::CGROUP_CONFIG_FAILED, "Failed to set memory limit to 1GB");

    const char* msg = error.what();
    std::string message_str(msg);

    EXPECT_TRUE(message_str.find("[docker-cpp 4001]") != std::string::npos);
    EXPECT_TRUE(message_str.find("Failed to configure cgroup") != std::string::npos);
    EXPECT_TRUE(message_str.find("Failed to set memory limit to 1GB") != std::string::npos);
}
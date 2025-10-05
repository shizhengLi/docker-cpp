#include <gtest/gtest.h>
#include <chrono>
#include <docker-cpp/core/logger.hpp>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace docker_cpp;

class LoggerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        test_dir = std::filesystem::temp_directory_path() / "docker_cpp_logger_test";
        std::filesystem::create_directories(test_dir);

        // Reset logger instance before each test
        Logger::resetInstance();
    }

    void TearDown() override
    {
        Logger::resetInstance();
        std::filesystem::remove_all(test_dir);
    }

protected:
    const std::filesystem::path& getTestDir() const
    {
        return test_dir;
    }

private:
    std::filesystem::path test_dir;
};

TEST_F(LoggerTest, DefaultConstructorCreatesDefaultLogger)
{
    auto* logger = Logger::getInstance();

    EXPECT_NE(logger, nullptr);
    EXPECT_EQ(logger->getLevel(), LogLevel::INFO);
    EXPECT_TRUE(logger->isLevelEnabled(LogLevel::INFO));
    EXPECT_FALSE(logger->isLevelEnabled(LogLevel::DEBUG));
}

TEST_F(LoggerTest, SetAndGetLogLevel)
{
    auto* logger = Logger::getInstance();

    logger->setLevel(LogLevel::DEBUG);
    EXPECT_EQ(logger->getLevel(), LogLevel::DEBUG);
    EXPECT_TRUE(logger->isLevelEnabled(LogLevel::DEBUG));
    EXPECT_TRUE(logger->isLevelEnabled(LogLevel::INFO));
    EXPECT_TRUE(logger->isLevelEnabled(LogLevel::WARNING));
    EXPECT_TRUE(logger->isLevelEnabled(LogLevel::ERROR));
    EXPECT_TRUE(logger->isLevelEnabled(LogLevel::CRITICAL));

    logger->setLevel(LogLevel::ERROR);
    EXPECT_EQ(logger->getLevel(), LogLevel::ERROR);
    EXPECT_FALSE(logger->isLevelEnabled(LogLevel::DEBUG));
    EXPECT_FALSE(logger->isLevelEnabled(LogLevel::INFO));
    EXPECT_FALSE(logger->isLevelEnabled(LogLevel::WARNING));
    EXPECT_TRUE(logger->isLevelEnabled(LogLevel::ERROR));
    EXPECT_TRUE(logger->isLevelEnabled(LogLevel::CRITICAL));
}

TEST_F(LoggerTest, BasicLoggingOperations)
{
    auto* logger = Logger::getInstance();
    logger->setLevel(LogLevel::DEBUG);

    // Capture console output
    testing::internal::CaptureStdout();

    logger->debug("Debug message");
    logger->info("Info message");
    logger->warning("Warning message");
    logger->error("Error message");
    logger->critical("Critical message");

    std::string output = testing::internal::GetCapturedStdout();

    EXPECT_NE(output.find("Debug message"), std::string::npos);
    EXPECT_NE(output.find("Info message"), std::string::npos);
    EXPECT_NE(output.find("Warning message"), std::string::npos);
    EXPECT_NE(output.find("Error message"), std::string::npos);
    EXPECT_NE(output.find("Critical message"), std::string::npos);
}

TEST_F(LoggerTest, LoggingWithParameters)
{
    auto* logger = Logger::getInstance();
    logger->setLevel(LogLevel::DEBUG);

    testing::internal::CaptureStdout();

    logger->info("User {} logged in from {}", "alice", "192.168.1.1");
    logger->error("Failed to connect to {}:{}", "localhost", 8080);
    logger->warning("Memory usage: {}%", 85.5);

    std::string output = testing::internal::GetCapturedStdout();

    EXPECT_NE(output.find("User alice logged in from 192.168.1.1"), std::string::npos);
    EXPECT_NE(output.find("Failed to connect to localhost:8080"), std::string::npos);
    EXPECT_NE(output.find("Memory usage: 85.5%"), std::string::npos);
}

TEST_F(LoggerTest, FileLogging)
{
    auto* logger = Logger::getInstance();
    std::filesystem::path log_file = getTestDir() / "test.log";

    logger->addFileSink(log_file, LogLevel::INFO);
    logger->setLevel(LogLevel::DEBUG);

    logger->debug("Debug message");
    logger->info("Info message");
    logger->error("Error message");

    // Give some time for async logging to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Flush the logger
    logger->flush();

    EXPECT_TRUE(std::filesystem::exists(log_file));

    std::ifstream file(log_file);
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    // Debug message should not appear (file sink level is INFO)
    EXPECT_EQ(content.find("Debug message"), std::string::npos);
    EXPECT_NE(content.find("Info message"), std::string::npos);
    EXPECT_NE(content.find("Error message"), std::string::npos);
}

TEST_F(LoggerTest, MultipleFileSinks)
{
    auto* logger = Logger::getInstance();
    std::filesystem::path info_file = getTestDir() / "info.log";
    std::filesystem::path error_file = getTestDir() / "error.log";

    logger->addFileSink(info_file, LogLevel::INFO);
    logger->addFileSink(error_file, LogLevel::ERROR);
    logger->setLevel(LogLevel::DEBUG);

    logger->debug("Debug message");
    logger->info("Info message");
    logger->warning("Warning message");
    logger->error("Error message");
    logger->critical("Critical message");

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    logger->flush();

    // Check info file
    std::ifstream info_fh(info_file);
    std::string info_content((std::istreambuf_iterator<char>(info_fh)),
                             std::istreambuf_iterator<char>());

    EXPECT_NE(info_content.find("Info message"), std::string::npos);
    EXPECT_NE(info_content.find("Warning message"), std::string::npos);
    EXPECT_NE(info_content.find("Error message"), std::string::npos);
    EXPECT_NE(info_content.find("Critical message"), std::string::npos);

    // Check error file
    std::ifstream error_fh(error_file);
    std::string error_content((std::istreambuf_iterator<char>(error_fh)),
                              std::istreambuf_iterator<char>());

    EXPECT_EQ(error_content.find("Debug message"), std::string::npos);
    EXPECT_EQ(error_content.find("Info message"), std::string::npos);
    EXPECT_EQ(error_content.find("Warning message"), std::string::npos);
    EXPECT_NE(error_content.find("Error message"), std::string::npos);
    EXPECT_NE(error_content.find("Critical message"), std::string::npos);
}

TEST_F(LoggerTest, CustomSink)
{
    auto* logger = Logger::getInstance();
    std::vector<std::string> captured_messages;

    auto custom_sink = [&](const LogMessage& message) {
        std::ostringstream oss;
        oss << "[" << toString(message.level) << "] " << message.message;
        captured_messages.push_back(oss.str());
    };

    logger->addSink(custom_sink, LogLevel::WARNING);
    logger->setLevel(LogLevel::DEBUG);

    logger->debug("Debug message");
    logger->info("Info message");
    logger->warning("Warning message");
    logger->error("Error message");

    logger->flush();

    EXPECT_EQ(captured_messages.size(), 2);
    EXPECT_NE(captured_messages[0].find("[WARNING] Warning message"), std::string::npos);
    EXPECT_NE(captured_messages[1].find("[ERROR] Error message"), std::string::npos);
}

TEST_F(LoggerTest, LogMessageFormatting)
{
    auto* logger = Logger::getInstance();
    (void)logger; // Suppress unused variable warning

    LogMessage msg;
    msg.level = LogLevel::ERROR;
    msg.message = "Test error";
    msg.logger_name = "test_logger";
    msg.thread_id = std::this_thread::get_id();
    msg.timestamp = std::chrono::system_clock::now();

    EXPECT_EQ(msg.level, LogLevel::ERROR);
    EXPECT_EQ(msg.message, "Test error");
    EXPECT_EQ(msg.logger_name, "test_logger");
    EXPECT_EQ(msg.thread_id, std::this_thread::get_id());
    EXPECT_FALSE(msg.timestamp.time_since_epoch().count() == 0);
}

TEST_F(LoggerTest, LoggerNaming)
{
    auto* default_logger = Logger::getInstance("default");
    auto* custom_logger = Logger::getInstance("custom");

    EXPECT_NE(default_logger, nullptr);
    EXPECT_NE(custom_logger, nullptr);
    EXPECT_EQ(default_logger->getName(), "default");
    EXPECT_EQ(custom_logger->getName(), "custom");

    // Same name should return same instance
    auto* same_logger = Logger::getInstance("default");
    EXPECT_EQ(default_logger, same_logger);
}

TEST_F(LoggerTest, RemoveFileSink)
{
    auto* logger = Logger::getInstance();
    std::filesystem::path log_file = getTestDir() / "test.log";

    logger->addFileSink(log_file, LogLevel::INFO);
    logger->info("Message before removal");

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    logger->removeFileSink(log_file);
    logger->info("Message after removal");

    logger->flush();

    std::ifstream file(log_file);
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    EXPECT_NE(content.find("Message before removal"), std::string::npos);
    EXPECT_EQ(content.find("Message after removal"), std::string::npos);
}

TEST_F(LoggerTest, ThreadSafety)
{
    auto* logger = Logger::getInstance();
    logger->setLevel(LogLevel::INFO);

    std::filesystem::path log_file = getTestDir() / "thread_test.log";
    logger->addFileSink(log_file, LogLevel::INFO);

    const int num_threads = 10;
    const int messages_per_thread = 100;
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&, i]() {
            for (int j = 0; j < messages_per_thread; ++j) {
                logger->info("Thread {} message {}", i, j);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    logger->flush();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::ifstream file(log_file);
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    // Count occurrences of "Thread" to verify all messages were logged
    size_t thread_count = 0;
    size_t pos = 0;
    while ((pos = content.find("Thread", pos)) != std::string::npos) {
        thread_count++;
        pos++;
    }

    EXPECT_EQ(thread_count, num_threads * messages_per_thread);
}

TEST_F(LoggerTest, LogLevelToString)
{
    EXPECT_EQ(toString(LogLevel::TRACE), "TRACE");
    EXPECT_EQ(toString(LogLevel::DEBUG), "DEBUG");
    EXPECT_EQ(toString(LogLevel::INFO), "INFO");
    EXPECT_EQ(toString(LogLevel::WARNING), "WARNING");
    EXPECT_EQ(toString(LogLevel::ERROR), "ERROR");
    EXPECT_EQ(toString(LogLevel::CRITICAL), "CRITICAL");
}

TEST_F(LoggerTest, StringToLogLevel)
{
    EXPECT_EQ(fromString("TRACE"), LogLevel::TRACE);
    EXPECT_EQ(fromString("DEBUG"), LogLevel::DEBUG);
    EXPECT_EQ(fromString("INFO"), LogLevel::INFO);
    EXPECT_EQ(fromString("WARNING"), LogLevel::WARNING);
    EXPECT_EQ(fromString("ERROR"), LogLevel::ERROR);
    EXPECT_EQ(fromString("CRITICAL"), LogLevel::CRITICAL);
    EXPECT_EQ(fromString("invalid"), LogLevel::INFO); // Default
}

TEST_F(LoggerTest, ConsoleSinkToggle)
{
    auto* logger = Logger::getInstance();

    testing::internal::CaptureStdout();
    logger->info("Message with console enabled");
    std::string output1 = testing::internal::GetCapturedStdout();

    logger->setConsoleSinkEnabled(false);
    testing::internal::CaptureStdout();
    logger->info("Message with console disabled");
    std::string output2 = testing::internal::GetCapturedStdout();

    logger->setConsoleSinkEnabled(true);
    testing::internal::CaptureStdout();
    logger->info("Message with console re-enabled");
    std::string output3 = testing::internal::GetCapturedStdout();

    EXPECT_NE(output1.find("Message with console enabled"), std::string::npos);
    EXPECT_EQ(output2.find("Message with console disabled"), std::string::npos);
    EXPECT_NE(output3.find("Message with console re-enabled"), std::string::npos);
}

TEST_F(LoggerTest, LogPatternFormatting)
{
    auto* logger = Logger::getInstance();
    logger->setPattern("[%l] %n: %v"); // [level] name: message

    testing::internal::CaptureStdout();
    logger->info("Test message");
    std::string output = testing::internal::GetCapturedStdout();

    EXPECT_NE(output.find("[INFO]"), std::string::npos);
    EXPECT_NE(output.find("Test message"), std::string::npos);
}

TEST_F(LoggerTest, LoggerReset)
{
    auto logger1 = Logger::getInstance("test");
    logger1->setLevel(LogLevel::ERROR);

    // Verify initial state
    EXPECT_EQ(logger1->getLevel(), LogLevel::ERROR);

    Logger::resetInstance("test");

    auto logger2 = Logger::getInstance("test");
    // Should return to default level after reset
    EXPECT_EQ(logger2->getLevel(), LogLevel::INFO);

    // The logger should be functional after reset
    logger2->info("Test message after reset");
}
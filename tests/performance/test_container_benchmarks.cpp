#include <gtest/gtest.h>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>
#include <atomic>
#include <future>
#include <algorithm>
#include <numeric>

#include "runtime/container.hpp"
#include "runtime/container_config.hpp"
#include "core/logger.hpp"
#include "core/event.hpp"

using namespace docker_cpp;
using namespace std::chrono;

// Performance benchmark test class for container operations
class ContainerPerformanceTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize core components
        logger_ = std::make_unique<core::Logger>();
        event_manager_ = std::make_unique<core::EventManager>();

        // Create basic container config for benchmarks
        config_.name = "benchmark-container";
        config_.image = "alpine:latest";
        config_.command = "/bin/sleep";
        config_.args = {"0.1"}; // Short lived process for benchmarks

        // Configure resource limits for benchmarks
        config_.resources.cpu_shares = 0.5;
        config_.resources.memory_limit = 64 * 1024 * 1024; // 64MB
        config_.resources.pids_limit = 5;
    }

    void TearDown() override {
        // Cleanup containers if any remain
        containers_.clear();
    }

    // Helper to measure execution time
    template<typename Func>
    nanoseconds measureExecutionTime(Func&& func) {
        auto start = high_resolution_clock::now();
        func();
        auto end = high_resolution_clock::now();
        return duration_cast<nanoseconds>(end - start);
    }

    // Helper to get median from a vector
    template<typename T>
    T getMedian(std::vector<T>& values) {
        std::sort(values.begin(), values.end());
        size_t size = values.size();
        if (size % 2 == 0) {
            return (values[size/2 - 1] + values[size/2]) / 2;
        } else {
            return values[size/2];
        }
    }

    // Helper to get statistics
    struct Stats {
        double mean_ms;
        double median_ms;
        double min_ms;
        double max_ms;
        double std_dev_ms;
    };

    Stats calculateStats(const std::vector<nanoseconds>& durations) {
        std::vector<double> ms_values;
        ms_values.reserve(durations.size());

        for (const auto& duration : durations) {
            ms_values.push_back(duration.count() / 1000000.0); // Convert to ms
        }

        double mean = std::accumulate(ms_values.begin(), ms_values.end(), 0.0) / ms_values.size();
        double median = getMedian(ms_values);
        double min_val = *std::min_element(ms_values.begin(), ms_values.end());
        double max_val = *std::max_element(ms_values.begin(), ms_values.end());

        // Calculate standard deviation
        double variance = 0.0;
        for (double val : ms_values) {
            variance += (val - mean) * (val - mean);
        }
        variance /= ms_values.size();
        double std_dev = std::sqrt(variance);

        return {mean, median, min_val, max_val, std_dev};
    }

    std::unique_ptr<core::Logger> logger_;
    std::unique_ptr<core::EventManager> event_manager_;
    ContainerConfig config_;
    std::vector<std::shared_ptr<Container>> containers_;
};

// Test container creation performance
TEST_F(ContainerPerformanceTest, ContainerCreationPerformance) {
    constexpr int iterations = 1000;
    std::vector<nanoseconds> durations;
    durations.reserve(iterations);

    // Warm-up
    for (int i = 0; i < 10; ++i) {
        auto container = std::make_shared<Container>(config_);
    }

    // Benchmark container creation
    for (int i = 0; i < iterations; ++i) {
        auto duration = measureExecutionTime([&]() {
            auto container = std::make_shared<Container>(config_);
            (void)container; // Suppress unused variable warning
        });
        durations.push_back(duration);
    }

    auto stats = calculateStats(durations);

    std::cout << "\n=== Container Creation Performance ===" << std::endl;
    std::cout << "Iterations: " << iterations << std::endl;
    std::cout << "Mean: " << stats.mean_ms << " ms" << std::endl;
    std::cout << "Median: " << stats.median_ms << " ms" << std::endl;
    std::cout << "Min: " << stats.min_ms << " ms" << std::endl;
    std::cout << "Max: " << stats.max_ms << " ms" << std::endl;
    std::cout << "Std Dev: " << stats.std_dev_ms << " ms" << std::endl;

    // Performance assertions
    EXPECT_LT(stats.mean_ms, 1.0) << "Container creation should be fast (< 1ms mean)";
    EXPECT_LT(stats.median_ms, 0.5) << "Container creation should be fast (< 0.5ms median)";
    EXPECT_LT(stats.max_ms, 10.0) << "Container creation should not be too slow (< 10ms max)";
}

// Test container startup performance
TEST_F(ContainerPerformanceTest, ContainerStartupPerformance) {
    constexpr int iterations = 100;
    std::vector<nanoseconds> durations;
    durations.reserve(iterations);

    // Benchmark container startup
    for (int i = 0; i < iterations; ++i) {
        auto container = std::make_shared<Container>(config_);

        auto duration = measureExecutionTime([&]() {
            container->start();
            // Wait a moment for startup to complete
            std::this_thread::sleep_for(milliseconds(10));
        });

        durations.push_back(duration);

        // Stop the container
        container->stop(1);
        container->remove();
    }

    auto stats = calculateStats(durations);

    std::cout << "\n=== Container Startup Performance ===" << std::endl;
    std::cout << "Iterations: " << iterations << std::endl;
    std::cout << "Mean: " << stats.mean_ms << " ms" << std::endl;
    std::cout << "Median: " << stats.median_ms << " ms" << std::endl;
    std::cout << "Min: " << stats.min_ms << " ms" << std::endl;
    std::cout << "Max: " << stats.max_ms << " ms" << std::endl;
    std::cout << "Std Dev: " << stats.std_dev_ms << " ms" << std::endl;

    // Performance assertions
    EXPECT_LT(stats.mean_ms, 100.0) << "Container startup should be fast (< 100ms mean)";
    EXPECT_LT(stats.median_ms, 50.0) << "Container startup should be fast (< 50ms median)";
}

// Test container stop performance
TEST_F(ContainerPerformanceTest, ContainerStopPerformance) {
    constexpr int iterations = 50;
    std::vector<nanoseconds> durations;
    durations.reserve(iterations);

    // Start containers first
    std::vector<std::shared_ptr<Container>> running_containers;
    for (int i = 0; i < iterations; ++i) {
        auto container = std::make_shared<Container>(config_);
        container->start();
        running_containers.push_back(container);
        std::this_thread::sleep_for(milliseconds(20)); // Let them start
    }

    // Benchmark container stopping
    for (auto& container : running_containers) {
        auto duration = measureExecutionTime([&]() {
            container->stop(5);
        });
        durations.push_back(duration);
    }

    auto stats = calculateStats(durations);

    std::cout << "\n=== Container Stop Performance ===" << std::endl;
    std::cout << "Iterations: " << iterations << std::endl;
    std::cout << "Mean: " << stats.mean_ms << " ms" << std::endl;
    std::cout << "Median: " << stats.median_ms << " ms" << std::endl;
    std::cout << "Min: " << stats.min_ms << " ms" << std::endl;
    std::cout << "Max: " << stats.max_ms << " ms" << std::endl;
    std::cout << "Std Dev: " << stats.std_dev_ms << " ms" << std::endl;

    // Performance assertions
    EXPECT_LT(stats.mean_ms, 500.0) << "Container stop should be fast (< 500ms mean)";
    EXPECT_LT(stats.median_ms, 200.0) << "Container stop should be fast (< 200ms median)";
}

// Test resource statistics collection performance
TEST_F(ContainerPerformanceTest, ResourceStatsCollectionPerformance) {
    constexpr int iterations = 1000;
    std::vector<nanoseconds> durations;
    durations.reserve(iterations);

    // Start a container for stats collection
    auto container = std::make_shared<Container>(config_);
    container->start();
    std::this_thread::sleep_for(milliseconds(50)); // Let it start

    // Benchmark stats collection
    for (int i = 0; i < iterations; ++i) {
        auto duration = measureExecutionTime([&]() {
            auto stats = container->getStats();
            (void)stats; // Suppress unused variable warning
        });
        durations.push_back(duration);
    }

    // Cleanup
    container->stop(1);
    container->remove();

    auto stats = calculateStats(durations);

    std::cout << "\n=== Resource Stats Collection Performance ===" << std::endl;
    std::cout << "Iterations: " << iterations << std::endl;
    std::cout << "Mean: " << stats.mean_ms << " ms" << std::endl;
    std::cout << "Median: " << stats.median_ms << " ms" << std::endl;
    std::cout << "Min: " << stats.min_ms << " ms" << std::endl;
    std::cout << "Max: " << stats.max_ms << " ms" << std::endl;
    std::cout << "Std Dev: " << stats.std_dev_ms << " ms" << std::endl;

    // Performance assertions
    EXPECT_LT(stats.mean_ms, 10.0) << "Stats collection should be fast (< 10ms mean)";
    EXPECT_LT(stats.median_ms, 5.0) << "Stats collection should be fast (< 5ms median)";
}

// Test container state transition performance
TEST_F(ContainerPerformanceTest, StateTransitionPerformance) {
    constexpr int iterations = 500;
    std::vector<nanoseconds> durations;
    durations.reserve(iterations);

    // Benchmark state transitions
    for (int i = 0; i < iterations; ++i) {
        auto container = std::make_shared<Container>(config_);

        auto duration = measureExecutionTime([&]() {
            container->start();
            container->stop(1);
            container->remove();
        });
        durations.push_back(duration);
    }

    auto stats = calculateStats(durations);

    std::cout << "\n=== State Transition Performance ===" << std::endl;
    std::cout << "Iterations: " << iterations << std::endl;
    std::cout << "Mean: " << stats.mean_ms << " ms" << std::endl;
    std::cout << "Median: " << stats.median_ms << " ms" << std::endl;
    std::cout << "Min: " << stats.min_ms << " ms" << std::endl;
    std::cout << "Max: " << stats.max_ms << " ms" << std::endl;
    std::cout << "Std Dev: " << stats.std_dev_ms << " ms" << std::endl;

    // Performance assertions
    EXPECT_LT(stats.mean_ms, 1000.0) << "State transitions should be fast (< 1000ms mean)";
}

// Test concurrent container operations performance
TEST_F(ContainerPerformanceTest, ConcurrentContainerOperationsPerformance) {
    constexpr int num_threads = 10;
    constexpr int containers_per_thread = 20;
    std::vector<std::future<void>> futures;
    std::atomic<int> total_operations{0};

    // Benchmark concurrent operations
    auto start_time = high_resolution_clock::now();

    for (int t = 0; t < num_threads; ++t) {
        futures.push_back(std::async(std::launch::async, [this, containers_per_thread, &total_operations]() {
            for (int i = 0; i < containers_per_thread; ++i) {
                auto container = std::make_shared<Container>(config_);

                // Perform container lifecycle operations
                container->start();
                std::this_thread::sleep_for(milliseconds(10)); // Short lifetime
                container->stop(1);
                container->remove();

                total_operations++;
            }
        }));
    }

    // Wait for all threads to complete
    for (auto& future : futures) {
        future.wait();
    }

    auto end_time = high_resolution_clock::now();
    auto total_duration = duration_cast<milliseconds>(end_time - start_time);

    std::cout << "\n=== Concurrent Container Operations Performance ===" << std::endl;
    std::cout << "Threads: " << num_threads << std::endl;
    std::cout << "Containers per thread: " << containers_per_thread << std::endl;
    std::cout << "Total operations: " << total_operations.load() << std::endl;
    std::cout << "Total time: " << total_duration.count() << " ms" << std::endl;
    std::cout << "Operations per second: " << (total_operations.load() * 1000.0 / total_duration.count()) << std::endl;

    // Performance assertions
    EXPECT_EQ(total_operations.load(), num_threads * containers_per_thread);
    EXPECT_LT(total_duration.count(), 30000) << "Concurrent operations should complete in reasonable time";
}

// Memory usage benchmark
TEST_F(ContainerPerformanceTest, MemoryUsageBenchmark) {
    constexpr int num_containers = 100;
    std::vector<std::shared_ptr<Container>> containers;

    // Measure memory usage with different container counts
    for (int i = 1; i <= num_containers; i *= 2) {
        containers.clear();
        containers.reserve(i);

        auto duration = measureExecutionTime([&]() {
            for (int j = 0; j < i; ++j) {
                auto container = std::make_shared<Container>(config_);
                containers.push_back(container);
            }
        });

        std::cout << "Created " << i << " containers in "
                  << duration.count() / 1000000.0 << " ms" << std::endl;

        // Performance assertion
        EXPECT_LT(duration.count() / 1000000.0, 100.0) << "Creating many containers should be fast";
    }
}
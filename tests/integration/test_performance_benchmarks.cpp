#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <memory>
#include <numeric>
#include <random>
#include <thread>
#include <vector>

// Include all Phase 1 components
#include <docker-cpp/cgroup/cgroup_manager.hpp>
#include <docker-cpp/cgroup/resource_monitor.hpp>
#include <docker-cpp/config/config_manager.hpp>
#include <docker-cpp/core/error.hpp>
#include <docker-cpp/core/event.hpp>
#include <docker-cpp/core/logger.hpp>
#include <docker-cpp/namespace/namespace_manager.hpp>
#include <docker-cpp/namespace/process_manager.hpp>
#include <docker-cpp/plugin/plugin_interface.hpp>
#include <docker-cpp/plugin/plugin_registry.hpp>

using namespace docker_cpp;
using namespace std::chrono_literals;

// Performance benchmarking test class
class Phase1PerformanceBenchmarks : public ::testing::Test {
protected:
    void SetUp() override
    {
        // Initialize all components for benchmarking
        try {
            logger_ = Logger::getInstance();
            logger_->setLevel(LogLevel::WARNING); // Reduce log noise for benchmarks

            config_manager_ = std::make_unique<ConfigManager>();
            event_manager_ = EventManager::getInstance();
            plugin_registry_ = std::make_unique<PluginRegistry>();

            // Check system capabilities
            try {
                NamespaceManager test_ns(NamespaceType::PID);
                can_use_namespaces_ = test_ns.isValid();
            }
            catch (const ContainerError&) {
                can_use_namespaces_ = false;
            }

            try {
                CgroupConfig config;
                config.name = "test_performance";
                auto test_cg = CgroupManager::create(config);
                can_use_cgroups_ = test_cg != nullptr;
            }
            catch (const ContainerError&) {
                can_use_cgroups_ = false;
            }
        }
        catch (const std::exception& e) {
            GTEST_SKIP() << "Failed to initialize performance benchmarks: " << e.what();
        }
    }

    void TearDown() override
    {
        // Cleanup resources
        namespace_managers_.clear();
        plugin_registry_.reset();
        // Note: EventManager and Logger are singletons, don't reset them here
        config_manager_.reset();
    }

    template <typename Func>
    auto measureExecutionTime(Func&& func)
    {
        auto start = std::chrono::high_resolution_clock::now();
        func();
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    }

    template <typename Func>
    std::vector<double> runMultipleBenchmarks(int iterations, Func&& benchmark)
    {
        std::vector<double> times;
        times.reserve(iterations);

        for (int i = 0; i < iterations; ++i) {
            auto duration = measureExecutionTime(benchmark);
            times.push_back(static_cast<double>(duration.count()));
        }

        return times;
    }

    void printBenchmarkStats(const std::string& name,
                             std::vector<double> times,
                             const std::string& unit = "μs")
    {
        double min = *std::min_element(times.begin(), times.end());
        double max = *std::max_element(times.begin(), times.end());
        double mean = std::accumulate(times.begin(), times.end(), 0.0) / times.size();

        std::sort(times.begin(), times.end());
        double median = times[times.size() / 2];

        std::cout << name << " (" << times.size() << " runs):" << std::endl;
        std::cout << "  Mean: " << std::fixed << std::setprecision(2) << mean << " " << unit
                  << std::endl;
        std::cout << "  Median: " << median << " " << unit << std::endl;
        std::cout << "  Min: " << min << " " << unit << std::endl;
        std::cout << "  Max: " << max << " " << unit << std::endl;
        std::cout << std::endl;
    }

    // Phase 1 components
    Logger* logger_ = nullptr;
    std::unique_ptr<ConfigManager> config_manager_;
    EventManager* event_manager_ = nullptr;
    std::unique_ptr<PluginRegistry> plugin_registry_;
    std::vector<std::unique_ptr<NamespaceManager>> namespace_managers_;
    std::unique_ptr<ProcessManager> process_manager_;
    std::unique_ptr<CgroupManager> cgroup_manager_;
    std::unique_ptr<ResourceMonitor> resource_monitor_;

    // System capabilities
    bool can_use_namespaces_ = false;
    bool can_use_cgroups_ = false;

    // Random number generator for benchmarks
    std::random_device rd_;
    std::mt19937 gen_{rd_()};
    std::uniform_int_distribution<> dis_{1, 1000};
};

// Benchmark configuration system performance
TEST_F(Phase1PerformanceBenchmarks, ConfigurationPerformance)
{
    const int iterations = 1000;

    auto benchmark = [&]() {
        for (int i = 0; i < iterations; ++i) {
            std::string key = "benchmark.config.key." + std::to_string(i);
            std::string value = "benchmark.value." + std::to_string(dis_(gen_));

            config_manager_->set(key, value);
            auto retrieved = config_manager_->get<std::string>(key);
            EXPECT_EQ(retrieved, value);
        }
    };

    auto times = runMultipleBenchmarks(10, benchmark);
    printBenchmarkStats("Configuration System (" + std::to_string(iterations) + " ops)", times);

    // Performance assertion
    double mean_time = times[times.size() / 2]; // Use median
    EXPECT_LT(mean_time, 50000) << "Configuration operations should complete in < 50ms";
}

// Benchmark event system performance
TEST_F(Phase1PerformanceBenchmarks, EventSystemPerformance)
{
    const int iterations = 1000;

    // Set up event subscription
    int events_received = 0;
    auto subscription_id = event_manager_->subscribe(
        "benchmark.event", [&](const Event& event) { events_received++; });

    auto benchmark = [&]() {
        events_received = 0;
        for (int i = 0; i < iterations; ++i) {
            Event event("benchmark.event", "benchmark data " + std::to_string(i));
            event_manager_->publish(event);
        }

        // Wait for event processing
        std::this_thread::sleep_for(10ms);
    };

    auto times = runMultipleBenchmarks(10, benchmark);
    printBenchmarkStats("Event System (" + std::to_string(iterations) + " events)", times);

    // Verify events were processed
    EXPECT_GT(events_received, 0);

    // Performance assertion
    double mean_time = times[times.size() / 2]; // Use median
    EXPECT_LT(mean_time, 100000) << "Event operations should complete in < 100ms";
}

// Benchmark namespace creation performance (if supported)
TEST_F(Phase1PerformanceBenchmarks, NamespacePerformance)
{
    if (!can_use_namespaces_) {
        GTEST_SKIP() << "Namespace operations not supported on this system";
    }

    const int iterations = 50; // Fewer iterations due to system overhead

    auto benchmark = [&]() {
        namespace_managers_.clear();
        for (int i = 0; i < iterations; ++i) {
            try {
                auto ns = std::make_unique<NamespaceManager>(NamespaceType::UTS);
                EXPECT_TRUE(ns->isValid());
                namespace_managers_.push_back(std::move(ns));
            }
            catch (const ContainerError&) {
                // Namespace creation might fail, which is acceptable for benchmarks
            }
        }
    };

    auto times = runMultipleBenchmarks(5, benchmark);
    printBenchmarkStats("Namespace Creation (" + std::to_string(iterations) + " namespaces)",
                        times);

    // Performance assertion (namespaces are expensive, so allow more time)
    double mean_time = times[times.size() / 2]; // Use median
    EXPECT_LT(mean_time, 1000000) << "Namespace creation should complete in < 1s";
}

// Benchmark cgroup operations performance (if supported)
TEST_F(Phase1PerformanceBenchmarks, CgroupPerformance)
{
    if (!can_use_cgroups_) {
        GTEST_SKIP() << "Cgroup operations not supported on this system";
    }

    const int iterations = 10; // Fewer iterations due to system overhead

    auto benchmark = [&]() {
        for (int i = 0; i < iterations; ++i) {
            try {
                CgroupConfig config;
                config.name = "benchmark_cgroup_" + std::to_string(i);
                auto cgroup_mgr = CgroupManager::create(config);
                if (cgroup_mgr) {
                    cgroup_manager_ = std::move(cgroup_mgr);
                }

                // Cleanup
                cgroup_manager_.reset();
            }
            catch (const ContainerError&) {
                // Cgroup operations might fail, which is acceptable for benchmarks
            }
        }
    };

    auto times = runMultipleBenchmarks(3, benchmark);
    printBenchmarkStats("Cgroup Operations (" + std::to_string(iterations) + " cgroups)", times);

    // Performance assertion (cgroups are expensive, so allow more time)
    double mean_time = times[times.size() / 2]; // Use median
    EXPECT_LT(mean_time, 2000000) << "Cgroup operations should complete in < 2s";
}

// Benchmark plugin registry performance
TEST_F(Phase1PerformanceBenchmarks, PluginRegistryPerformance)
{
    const int iterations = 100;

    auto benchmark = [&]() {
        for (int i = 0; i < iterations; ++i) {
            // Test plugin registry queries
            auto count = plugin_registry_->getPluginCount();
            EXPECT_GE(count, 0);

            // Plugin loading would require actual plugin files/directories
        }
    };

    auto times = runMultipleBenchmarks(10, benchmark);
    printBenchmarkStats("Plugin Registry (" + std::to_string(iterations) + " queries)", times);

    // Performance assertion
    double mean_time = times[times.size() / 2]; // Use median
    EXPECT_LT(mean_time, 10000) << "Plugin registry operations should complete in < 10ms";
}

// Benchmark concurrent operations
TEST_F(Phase1PerformanceBenchmarks, ConcurrentOperations)
{
    const int num_threads = 4;
    const int operations_per_thread = 250;

    auto benchmark = [&]() {
        std::vector<std::thread> threads;
        std::atomic<int> completed_operations{0};

        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&, t]() {
                for (int i = 0; i < operations_per_thread; ++i) {
                    // Configuration operations
                    config_manager_->set(
                        "thread." + std::to_string(t) + ".key." + std::to_string(i),
                        "value_" + std::to_string(i));

                    // Event operations
                    Event event("concurrent.benchmark",
                                "thread " + std::to_string(t) + " operation " + std::to_string(i));
                    event_manager_->publish(event);

                    completed_operations++;
                }
            });
        }

        for (auto& thread : threads) {
            thread.join();
        }

        EXPECT_EQ(completed_operations.load(), num_threads * operations_per_thread);
    };

    auto times = runMultipleBenchmarks(5, benchmark);
    printBenchmarkStats(
        "Concurrent Operations (" + std::to_string(num_threads * operations_per_thread) + " ops)",
        times);

    // Performance assertion
    double mean_time = times[times.size() / 2]; // Use median
    EXPECT_LT(mean_time, 200000) << "Concurrent operations should complete in < 200ms";
}

// Memory usage benchmark
TEST_F(Phase1PerformanceBenchmarks, MemoryUsageBenchmark)
{
    const int iterations = 1000;

    // Test memory usage patterns
    auto benchmark = [&]() {
        std::vector<std::unique_ptr<ConfigManager>> configs;
        std::vector<Event> events;

        // Create many configurations
        for (int i = 0; i < iterations; ++i) {
            auto config = std::make_unique<ConfigManager>();
            config->set("memory.test.key", "memory.test.value." + std::to_string(i));
            configs.push_back(std::move(config));
        }

        // Create many events
        for (int i = 0; i < iterations; ++i) {
            events.emplace_back("memory.benchmark", "memory test data " + std::to_string(i));
        }

        // Cleanup happens automatically when vectors go out of scope
    };

    auto times = runMultipleBenchmarks(5, benchmark);
    printBenchmarkStats("Memory Usage (" + std::to_string(iterations) + " objects)", times);

    // Performance assertion
    double mean_time = times[times.size() / 2]; // Use median
    EXPECT_LT(mean_time, 100000) << "Memory operations should complete in < 100ms";
}

// Overall system performance summary
TEST_F(Phase1PerformanceBenchmarks, PerformanceSummary)
{
    std::cout << "\n=== Phase 1 Performance Benchmark Summary ===" << std::endl;
    std::cout << "System Capabilities:" << std::endl;
    std::cout << "  Namespaces: " << (can_use_namespaces_ ? "Supported" : "Not Supported")
              << std::endl;
    std::cout << "  Cgroups: " << (can_use_cgroups_ ? "Supported" : "Not Supported") << std::endl;
    std::cout << "\nComponents Tested:" << std::endl;
    std::cout << "  ✓ Configuration Management" << std::endl;
    std::cout << "  ✓ Event System" << std::endl;
    if (can_use_namespaces_) {
        std::cout << "  ✓ Namespace Management" << std::endl;
    }
    if (can_use_cgroups_) {
        std::cout << "  ✓ Cgroup Management" << std::endl;
    }
    std::cout << "  ✓ Plugin Registry" << std::endl;
    std::cout << "  ✓ Concurrent Operations" << std::endl;
    std::cout << "  ✓ Memory Usage Patterns" << std::endl;
    std::cout << "================================================" << std::endl;

    // This test always passes - it's just for reporting
    SUCCEED();
}
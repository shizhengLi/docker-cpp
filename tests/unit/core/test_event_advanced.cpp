#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <docker-cpp/core/event.hpp>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class EventAdvancedTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // Reset event system before each test
        docker_cpp::EventManager::resetInstance();
    }

    void TearDown() override
    {
        docker_cpp::EventManager::resetInstance();
    }
};

// Test high-frequency event processing performance
TEST_F(EventAdvancedTest, HighFrequencyEventProcessing)
{
    auto* manager = docker_cpp::EventManager::getInstance();

    std::atomic<int> received_count{0};
    std::mutex received_mutex;
    std::vector<docker_cpp::Event> received_events;

    docker_cpp::EventListener listener = [&](const docker_cpp::Event& event) {
        std::lock_guard<std::mutex> lock(received_mutex);
        received_events.push_back(event);
        received_count++;
    };

    manager->subscribe("performance.test", listener);

    const int num_events = 10000;
    auto start_time = std::chrono::high_resolution_clock::now();

    // Publish events rapidly
    for (int i = 0; i < num_events; ++i) {
        docker_cpp::Event event("performance.test", "Event " + std::to_string(i));
        manager->publish(event);
    }

    // Wait for processing to complete
    while (received_count < num_events) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    // Performance assertions
    EXPECT_EQ(received_count, num_events);
    EXPECT_LT(duration.count(), 1000); // Should process 10,000 events in under 1 second

    // Verify all events were received in order
    EXPECT_EQ(received_events.size(), num_events);
    for (int i = 0; i < num_events; ++i) {
        EXPECT_EQ(received_events[i].getData(), "Event " + std::to_string(i));
    }
}

// Test complex event batching scenarios
TEST_F(EventAdvancedTest, ComplexEventBatching)
{
    auto* manager = docker_cpp::EventManager::getInstance();

    std::vector<docker_cpp::Event> received_events;
    std::mutex received_mutex;

    docker_cpp::EventListener listener = [&](const docker_cpp::Event& event) {
        std::lock_guard<std::mutex> lock(received_mutex);
        received_events.push_back(event);
    };

    // Configure batching
    manager->subscribe("batch.test", listener);
    manager->enableBatching("batch.test", std::chrono::milliseconds(50), 10);

    // Publish events in rapid succession
    for (int i = 0; i < 25; ++i) {
        docker_cpp::Event event("batch.test", "Batch event " + std::to_string(i));
        manager->publish(event);
    }

    // Wait for batch processing to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Should receive all events in batches
    EXPECT_EQ(received_events.size(), 25);

    // Verify event content
    for (int i = 0; i < 25; ++i) {
        EXPECT_EQ(received_events[i].getData(), "Batch event " + std::to_string(i));
    }

    // Disable batching and verify events are processed individually
    manager->disableBatching("batch.test");
    received_events.clear();

    // Publish more events
    for (int i = 25; i < 30; ++i) {
        docker_cpp::Event event("batch.test", "Individual event " + std::to_string(i));
        manager->publish(event);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_EQ(received_events.size(), 5);
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(received_events[i].getData(), "Individual event " + std::to_string(i + 25));
    }
}

// Test event system under high concurrent load
TEST_F(EventAdvancedTest, EventSystemConcurrencyStress)
{
    auto* manager = docker_cpp::EventManager::getInstance();

    std::atomic<int> total_received{0};
    std::atomic<int> publisher_count{0};
    std::unordered_map<int, int> thread_event_counts;
    std::mutex counts_mutex;

    // Multiple subscribers
    const int num_subscribers = 5;
    std::vector<docker_cpp::SubscriptionId> subscription_ids;

    for (int i = 0; i < num_subscribers; ++i) {
        docker_cpp::EventListener listener = [&](const docker_cpp::Event& event) {
            total_received++;

            // Extract thread ID from event data
            std::string data = event.getData();
            size_t thread_pos = data.find("thread-");
            if (thread_pos != std::string::npos) {
                size_t end_pos = data.find(" ", thread_pos);
                std::string thread_str = data.substr(thread_pos + 7, end_pos - thread_pos - 7);
                int thread_id = std::stoi(thread_str);

                std::lock_guard<std::mutex> lock(counts_mutex);
                thread_event_counts[thread_id]++;
            }
        };

        subscription_ids.push_back(manager->subscribe("stress.test", listener));
    }

    const int num_threads = 10;
    const int events_per_thread = 1000;
    std::vector<std::thread> threads;

    // Start publisher threads
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([i, &publisher_count]() {
            for (int j = 0; j < events_per_thread; ++j) {
                docker_cpp::Event event(
                    "stress.test", "thread-" + std::to_string(i) + " event-" + std::to_string(j));
                docker_cpp::EventManager::getInstance()->publish(event);
                publisher_count++;
            }
        });
    }

    // Wait for all publishers to complete
    for (auto& thread : threads) {
        thread.join();
    }

    // Wait for event processing to complete
    int expected_total = num_threads * events_per_thread * num_subscribers;
    auto start_time = std::chrono::high_resolution_clock::now();

    while (total_received < expected_total) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - start_time);
        if (elapsed.count() > 5000) { // 5 second timeout
            break;
        }
    }

    // Verify results
    EXPECT_EQ(publisher_count, num_threads * events_per_thread);

    // Allow for some processing delay but should receive most events
    EXPECT_GT(total_received, expected_total * 0.8); // At least 80% received

    // Verify thread distribution is relatively even
    std::lock_guard<std::mutex> lock(counts_mutex);
    EXPECT_EQ(thread_event_counts.size(), num_threads);

    for (const auto& [thread_id, count] : thread_event_counts) {
        EXPECT_EQ(count, events_per_thread * num_subscribers);
    }
}

// Test event priority queue behavior under load
TEST_F(EventAdvancedTest, EventPriorityQueueUnderLoad)
{
    auto* manager = docker_cpp::EventManager::getInstance();

    std::vector<docker_cpp::Event> received_events;
    std::mutex received_mutex;

    docker_cpp::EventListener listener = [&](const docker_cpp::Event& event) {
        std::lock_guard<std::mutex> lock(received_mutex);
        received_events.push_back(event);
    };

    manager->subscribe("priority.test", listener);

    // Publish events with mixed priorities
    std::vector<std::pair<docker_cpp::EventPriority, std::string>> test_events = {
        {docker_cpp::EventPriority::LOW, "low-1"},
        {docker_cpp::EventPriority::HIGH, "high-1"},
        {docker_cpp::EventPriority::NORMAL, "normal-1"},
        {docker_cpp::EventPriority::HIGH, "high-2"},
        {docker_cpp::EventPriority::LOW, "low-2"},
        {docker_cpp::EventPriority::NORMAL, "normal-2"},
        {docker_cpp::EventPriority::HIGH, "high-3"},
        {docker_cpp::EventPriority::CRITICAL, "critical-1"},
        {docker_cpp::EventPriority::LOW, "low-3"},
        {docker_cpp::EventPriority::NORMAL, "normal-3"}};

    // Publish events rapidly
    for (const auto& [priority, data] : test_events) {
        docker_cpp::Event event("priority.test", data, std::chrono::system_clock::now(), priority);
        manager->publish(event);
    }

    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Verify priority order
    EXPECT_EQ(received_events.size(), test_events.size());

    // Check that critical events come first
    auto critical_pos = std::find_if(
        received_events.begin(), received_events.end(), [](const docker_cpp::Event& e) {
            return e.getData() == "critical-1";
        });
    EXPECT_NE(critical_pos, received_events.end());
    EXPECT_EQ(critical_pos - received_events.begin(), 0); // Critical should be first

    // Check that high priority events come before normal and low
    auto high_count = std::count_if(
        received_events.begin(), received_events.end(), [](const docker_cpp::Event& e) {
            return e.getData().find("high") != std::string::npos;
        });
    EXPECT_EQ(high_count, 3);

    // Verify all high priority events come before normal ones
    size_t last_high_pos = 0;
    size_t first_normal_pos = received_events.size();
    for (size_t i = 0; i < received_events.size(); ++i) {
        if (received_events[i].getData().find("high") != std::string::npos) {
            last_high_pos = i;
        }
        else if (received_events[i].getData().find("normal") != std::string::npos
                 && first_normal_pos == received_events.size()) {
            first_normal_pos = i;
        }
    }
    EXPECT_LT(last_high_pos, first_normal_pos);
}

// Test event manager resource management
TEST_F(EventAdvancedTest, EventManagerResourceManagement)
{
    auto* manager = docker_cpp::EventManager::getInstance();

    // Set a smaller queue size for testing
    manager->setMaxQueueSize(100);

    std::atomic<int> received_count{0};
    docker_cpp::EventListener listener = [&](const docker_cpp::Event& /* event */) {
        received_count++;
        // Simulate slow processing
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    };

    manager->subscribe("resource.test", listener);

    // Publish more events than queue can hold
    const int total_events = 200;
    for (int i = 0; i < total_events; ++i) {
        docker_cpp::Event event("resource.test", "Resource event " + std::to_string(i));
        manager->publish(event);
    }

    // Wait for processing to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Should have received at least some events (queue size + some overflow tolerance)
    EXPECT_GT(received_count, 50);

    // Check statistics
    auto stats = manager->getStatistics();
    EXPECT_GT(stats.total_events_published, total_events - 50); // Most should have been published
    EXPECT_GT(stats.total_events_processed, 50);
    EXPECT_EQ(stats.pending_events, 0); // Queue should be empty after processing
}

// Test event metadata performance
TEST_F(EventAdvancedTest, EventMetadataPerformance)
{
    docker_cpp::Event event("metadata.test", "Base data");

    const int num_metadata_items = 1000;
    auto start_time = std::chrono::high_resolution_clock::now();

    // Add many metadata items
    for (int i = 0; i < num_metadata_items; ++i) {
        event.setMetadata("key" + std::to_string(i), "value" + std::to_string(i));
    }

    auto add_time = std::chrono::high_resolution_clock::now();
    auto add_duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(add_time - start_time);

    // Retrieve metadata items
    start_time = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < num_metadata_items; ++i) {
        auto value = event.getMetadata<std::string>("key" + std::to_string(i));
        EXPECT_EQ(value, "value" + std::to_string(i));
    }

    auto retrieve_time = std::chrono::high_resolution_clock::now();
    auto retrieve_duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(retrieve_time - start_time);

    // Performance assertions
    EXPECT_LT(add_duration.count(), 100);      // Should add 1000 metadata items in under 100ms
    EXPECT_LT(retrieve_duration.count(), 100); // Should retrieve 1000 items in under 100ms

    // Test metadata operations
    EXPECT_TRUE(event.hasMetadata("key500"));
    EXPECT_FALSE(event.hasMetadata("nonexistent"));

    event.removeMetadata("key500");
    EXPECT_FALSE(event.hasMetadata("key500"));

    // Check remaining count
    int remaining_count = 0;
    for (int i = 0; i < num_metadata_items; ++i) {
        if (event.hasMetadata("key" + std::to_string(i))) {
            remaining_count++;
        }
    }
    EXPECT_EQ(remaining_count, num_metadata_items - 1);
}

// Test event filtering performance
TEST_F(EventAdvancedTest, EventFilteringPerformance)
{
    auto* manager = docker_cpp::EventManager::getInstance();

    std::atomic<int> received_count{0};

    // Subscribe to multiple filter patterns
    std::vector<std::string> filters = {"filter.test.*",
                                        "filter.test.category1.*",
                                        "filter.test.category2.*",
                                        "filter.test.category3.subcategory.*"};

    std::vector<docker_cpp::SubscriptionId> subscription_ids;
    for (const auto& filter : filters) {
        docker_cpp::EventListener listener = [&](const docker_cpp::Event& /* event */) {
            received_count++;
        };
        subscription_ids.push_back(manager->subscribe(filter, listener));
    }

    const int num_events = 5000;
    auto start_time = std::chrono::high_resolution_clock::now();

    // Publish events with various patterns
    for (int i = 0; i < num_events; ++i) {
        std::string event_type;
        if (i % 4 == 0) {
            event_type = "filter.test.general";
        }
        else if (i % 4 == 1) {
            event_type = "filter.test.category1.event" + std::to_string(i);
        }
        else if (i % 4 == 2) {
            event_type = "filter.test.category2.event" + std::to_string(i);
        }
        else {
            event_type = "filter.test.category3.subcategory.event" + std::to_string(i);
        }

        docker_cpp::Event event(event_type, "Filter test event " + std::to_string(i));
        manager->publish(event);
    }

    // Calculate expected count based on pattern matching
    // Pattern "filter.test.*" matches all 5000 events
    // Pattern "filter.test.category1.*" matches 1250 events (i % 4 == 1)
    // Pattern "filter.test.category2.*" matches 1250 events (i % 4 == 2)
    // Pattern "filter.test.category3.subcategory.*" matches 1250 events (i % 4 == 3)
    // Total = 5000 + 1250 + 1250 + 1250 = 8750
    int expected_count = 8750;
    while (received_count < expected_count) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - start_time);
        if (elapsed.count() > 2000) { // 2 second timeout
            break;
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    // Performance assertions
    EXPECT_EQ(received_count, expected_count); // Each event should match expected number of filters
    EXPECT_LT(duration.count(),
              2000); // Should process 5000 events with complex filtering in under 2 seconds
}

// Test event manager recovery from errors
TEST_F(EventAdvancedTest, EventManagerErrorRecovery)
{
    auto* manager = docker_cpp::EventManager::getInstance();

    std::atomic<int> normal_received{0};
    std::atomic<int> error_received{0};

    // Normal listener
    docker_cpp::EventListener normal_listener = [&](const docker_cpp::Event& /* event */) {
        normal_received++;
    };

    // Error-prone listener
    docker_cpp::EventListener error_listener = [&](const docker_cpp::Event& event) {
        if (event.getData().find("cause_error") != std::string::npos) {
            throw std::runtime_error("Intentional listener error");
        }
        error_received++;
    };

    manager->subscribe("recovery.test", normal_listener);
    manager->subscribe("recovery.test", error_listener);

    // Publish normal events
    for (int i = 0; i < 10; ++i) {
        docker_cpp::Event event("recovery.test", "Normal event " + std::to_string(i));
        manager->publish(event);
    }

    // Publish error-causing event
    docker_cpp::Event error_event("recovery.test", "cause_error event");
    manager->publish(error_event);

    // Publish more normal events after error
    for (int i = 10; i < 20; ++i) {
        docker_cpp::Event event("recovery.test", "Normal event " + std::to_string(i));
        manager->publish(event);
    }

    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Normal listener should receive all events
    EXPECT_EQ(normal_received, 21); // 20 normal + 1 error

    // Error listener should receive events before and after the error
    EXPECT_GT(error_received, 15); // At least some events before error + some after recovery
    EXPECT_LT(error_received, 21); // Should miss at least the error event

    // System should still be functional
    auto stats = manager->getStatistics();
    EXPECT_GT(stats.total_events_published, 20);
    EXPECT_GT(stats.total_events_processed, 15);
}
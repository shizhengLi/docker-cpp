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

class EventTest : public ::testing::Test {
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

// Test basic event creation and properties
TEST_F(EventTest, BasicEventCreation)
{
    docker_cpp::Event event("test.event", "Test event data");

    EXPECT_EQ(event.getType(), "test.event");
    EXPECT_EQ(event.getData(), "Test event data");
    EXPECT_FALSE(event.getTimestamp().time_since_epoch().count() == 0);
    EXPECT_GT(event.getId(), 0);
}

TEST_F(EventTest, EventWithCustomTimestamp)
{
    auto custom_time = std::chrono::system_clock::now() - std::chrono::hours(1);
    docker_cpp::Event event("test.event", "Test data", custom_time);

    EXPECT_EQ(event.getTimestamp(), custom_time);
}

TEST_F(EventTest, EventCopyAndMove)
{
    docker_cpp::Event original("test.event", "Original data");
    docker_cpp::EventId original_id = original.getId();

    // Test copy
    docker_cpp::Event copy(original);
    EXPECT_EQ(copy.getType(), original.getType());
    EXPECT_EQ(copy.getData(), original.getData());
    EXPECT_EQ(copy.getTimestamp(), original.getTimestamp());
    EXPECT_EQ(copy.getId(), original_id);

    // Test move
    docker_cpp::Event moved(std::move(copy));
    EXPECT_EQ(moved.getType(), original.getType());
    EXPECT_EQ(moved.getData(), original.getData());
    EXPECT_EQ(moved.getTimestamp(), original.getTimestamp());
    EXPECT_EQ(moved.getId(), original_id);
}

// Test event manager basic functionality
TEST_F(EventTest, EventManagerSingleton)
{
    auto* manager1 = docker_cpp::EventManager::getInstance();
    auto* manager2 = docker_cpp::EventManager::getInstance();

    EXPECT_EQ(manager1, manager2);
}

TEST_F(EventTest, BasicEventPublishing)
{
    auto* manager = docker_cpp::EventManager::getInstance();

    std::vector<docker_cpp::Event> received_events;
    docker_cpp::EventListener listener = [&](const docker_cpp::Event& event) {
        received_events.push_back(event);
    };

    manager->subscribe("test.event", listener);

    docker_cpp::Event event("test.event", "Test data");
    manager->publish(event);

    // Wait for async event processing
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_EQ(received_events.size(), 1);
    EXPECT_EQ(received_events[0].getType(), "test.event");
    EXPECT_EQ(received_events[0].getData(), "Test data");
}

TEST_F(EventTest, MultipleSubscribers)
{
    auto* manager = docker_cpp::EventManager::getInstance();

    std::vector<docker_cpp::Event> received1, received2;
    docker_cpp::EventListener listener1 = [&](const docker_cpp::Event& event) {
        received1.push_back(event);
    };
    docker_cpp::EventListener listener2 = [&](const docker_cpp::Event& event) {
        received2.push_back(event);
    };

    manager->subscribe("test.event", listener1);
    manager->subscribe("test.event", listener2);

    docker_cpp::Event event("test.event", "Test data");
    manager->publish(event);

    EXPECT_EQ(received1.size(), 1);
    EXPECT_EQ(received2.size(), 1);
    EXPECT_EQ(received1[0].getData(), "Test data");
    EXPECT_EQ(received2[0].getData(), "Test data");
}

TEST_F(EventTest, EventFiltering)
{
    auto* manager = docker_cpp::EventManager::getInstance();

    std::vector<docker_cpp::Event> received_events;
    docker_cpp::EventListener listener = [&](const docker_cpp::Event& event) {
        received_events.push_back(event);
    };

    manager->subscribe("test.event", listener);

    // Publish events of different types
    docker_cpp::Event event1("test.event", "Should receive");
    docker_cpp::Event event2("other.event", "Should not receive");
    docker_cpp::Event event3("test.event", "Should also receive");

    manager->publish(event1);
    manager->publish(event2);
    manager->publish(event3);

    EXPECT_EQ(received_events.size(), 2);
    EXPECT_EQ(received_events[0].getData(), "Should receive");
    EXPECT_EQ(received_events[1].getData(), "Should also receive");
}

TEST_F(EventTest, UnsubscribeEvents)
{
    auto* manager = docker_cpp::EventManager::getInstance();

    std::vector<docker_cpp::Event> received_events;
    docker_cpp::EventListener listener = [&](const docker_cpp::Event& event) {
        received_events.push_back(event);
    };

    docker_cpp::SubscriptionId subscription = manager->subscribe("test.event", listener);

    docker_cpp::Event event1("test.event", "Before unsubscribe");
    manager->publish(event1);

    manager->unsubscribe(subscription);

    docker_cpp::Event event2("test.event", "After unsubscribe");
    manager->publish(event2);

    EXPECT_EQ(received_events.size(), 1);
    EXPECT_EQ(received_events[0].getData(), "Before unsubscribe");
}

TEST_F(EventTest, WildcardSubscriptions)
{
    auto* manager = docker_cpp::EventManager::getInstance();

    std::vector<docker_cpp::Event> received_events;
    docker_cpp::EventListener listener = [&](const docker_cpp::Event& event) {
        received_events.push_back(event);
    };

    manager->subscribe("test.*", listener);

    docker_cpp::Event event1("test.event1", "Should receive");
    docker_cpp::Event event2("test.event2", "Should receive");
    docker_cpp::Event event3("other.event", "Should not receive");

    manager->publish(event1);
    manager->publish(event2);
    manager->publish(event3);

    EXPECT_EQ(received_events.size(), 2);
    EXPECT_EQ(received_events[0].getType(), "test.event1");
    EXPECT_EQ(received_events[1].getType(), "test.event2");
}

TEST_F(EventTest, ThreadSafety)
{
    auto* manager = docker_cpp::EventManager::getInstance();

    std::vector<docker_cpp::Event> received_events;
    std::mutex received_mutex;

    docker_cpp::EventListener listener = [&](const docker_cpp::Event& event) {
        std::lock_guard<std::mutex> lock(received_mutex);
        received_events.push_back(event);
    };

    manager->subscribe("test.event", listener);

    const int num_threads = 10;
    const int events_per_thread = 100;
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&, i]() {
            for (int j = 0; j < events_per_thread; ++j) {
                docker_cpp::Event event(
                    "test.event",
                    "Thread " + std::to_string(i) + " docker_cpp::Event " + std::to_string(j));
                manager->publish(event);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // Give some time for all events to be processed
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::lock_guard<std::mutex> lock(received_mutex);
    EXPECT_EQ(received_events.size(), num_threads * events_per_thread);
}

TEST_F(EventTest, EventQueueing)
{
    auto* manager = docker_cpp::EventManager::getInstance();

    std::vector<docker_cpp::Event> received_events;
    docker_cpp::EventListener listener = [&](const docker_cpp::Event& event) {
        received_events.push_back(event);
    };

    manager->subscribe("test.event", listener);

    // Publish multiple events rapidly
    for (int i = 0; i < 100; ++i) {
        docker_cpp::Event event("test.event", "docker_cpp::Event " + std::to_string(i));
        manager->publish(event);
    }

    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_EQ(received_events.size(), 100);

    // Verify order preservation
    for (int i = 0; i < 100; ++i) {
        EXPECT_EQ(received_events[i].getData(), "docker_cpp::Event " + std::to_string(i));
    }
}

TEST_F(EventTest, EventMetadata)
{
    docker_cpp::Event event("test.event", "Test data");

    // Test metadata operations
    event.setMetadata("key1", "value1");
    event.setMetadata("key2", 42);
    event.setMetadata("key3", 3.14);

    EXPECT_EQ(event.getMetadata<std::string>("key1"), "value1");
    EXPECT_EQ(event.getMetadata<int>("key2"), 42);
    EXPECT_DOUBLE_EQ(event.getMetadata<double>("key3"), 3.14);

    EXPECT_TRUE(event.hasMetadata("key1"));
    EXPECT_FALSE(event.hasMetadata("nonexistent"));

    event.removeMetadata("key1");
    EXPECT_FALSE(event.hasMetadata("key1"));
}

TEST_F(EventTest, EventPriority)
{
    auto* manager = docker_cpp::EventManager::getInstance();

    std::vector<docker_cpp::Event> received_events;
    docker_cpp::EventListener listener = [&](const docker_cpp::Event& event) {
        received_events.push_back(event);
    };

    manager->subscribe("test.event", listener);

    // Publish events with different priorities
    docker_cpp::Event low_priority("test.event",
                                   "Low priority",
                                   std::chrono::system_clock::now(),
                                   docker_cpp::EventPriority::LOW);
    docker_cpp::Event normal_priority("test.event",
                                      "Normal priority",
                                      std::chrono::system_clock::now(),
                                      docker_cpp::EventPriority::NORMAL);
    docker_cpp::Event high_priority("test.event",
                                    "High priority",
                                    std::chrono::system_clock::now(),
                                    docker_cpp::EventPriority::HIGH);

    manager->publish(low_priority);
    manager->publish(normal_priority);
    manager->publish(high_priority);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_EQ(received_events.size(), 3);
    // High priority events should be processed first
    EXPECT_EQ(received_events[0].getData(), "High priority");
    EXPECT_EQ(received_events[1].getData(), "Normal priority");
    EXPECT_EQ(received_events[2].getData(), "Low priority");
}

TEST_F(EventTest, EventStatistics)
{
    auto* manager = docker_cpp::EventManager::getInstance();

    docker_cpp::EventListener listener =
        [](const docker_cpp::Event& /*event*/) { /* Do nothing */ };

    manager->subscribe("test.event1", listener);
    manager->subscribe("test.event2", listener);

    // Publish events
    manager->publish(docker_cpp::Event("test.event1", "docker_cpp::Event 1"));
    manager->publish(docker_cpp::Event("test.event1", "docker_cpp::Event 2"));
    manager->publish(docker_cpp::Event("test.event2", "docker_cpp::Event 3"));

    auto stats = manager->getStatistics();

    EXPECT_EQ(stats.total_events_published, 3);
    EXPECT_EQ(stats.total_events_processed, 3);
    EXPECT_EQ(stats.active_subscriptions, 2);
    EXPECT_EQ(stats.pending_events, 0);
}

TEST_F(EventTest, EventBatching)
{
    auto* manager = docker_cpp::EventManager::getInstance();

    std::vector<docker_cpp::Event> received_events;
    docker_cpp::EventListener listener = [&](const docker_cpp::Event& event) {
        received_events.push_back(event);
    };

    manager->subscribe("test.event", listener);

    // Enable batching
    manager->enableBatching("test.event", std::chrono::milliseconds(50), 10);

    // Publish events rapidly
    for (int i = 0; i < 25; ++i) {
        docker_cpp::Event event("test.event", "docker_cpp::Event " + std::to_string(i));
        manager->publish(event);
    }

    // Wait for batch processing
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Should receive events in batches
    EXPECT_EQ(received_events.size(), 25);
}

TEST_F(EventTest, AsyncEventProcessing)
{
    auto* manager = docker_cpp::EventManager::getInstance();

    std::vector<docker_cpp::Event> received_events;
    std::atomic<int> processing_count{0};

    docker_cpp::EventListener listener = [&](const docker_cpp::Event& event) {
        processing_count++;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        received_events.push_back(event);
    };

    manager->subscribe("test.event", listener);

    // Publish events asynchronously
    for (int i = 0; i < 5; ++i) {
        docker_cpp::Event event("test.event", "docker_cpp::Event " + std::to_string(i));
        manager->publish(event);
    }

    // Should return immediately (async processing)
    EXPECT_LT(received_events.size(), 5);

    // Wait for all events to be processed
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    EXPECT_EQ(received_events.size(), 5);
    EXPECT_EQ(processing_count, 5);
}

TEST_F(EventTest, ErrorHandlingInListeners)
{
    auto* manager = docker_cpp::EventManager::getInstance();

    std::vector<docker_cpp::Event> received_events;

    // Listener that throws an exception
    docker_cpp::EventListener faulty_listener = [&](const docker_cpp::Event& event) {
        if (event.getData() == "throw") {
            throw std::runtime_error("Test exception");
        }
        received_events.push_back(event);
    };

    // Normal listener
    docker_cpp::EventListener normal_listener = [&](const docker_cpp::Event& event) {
        received_events.push_back(event);
    };

    manager->subscribe("test.event", faulty_listener);
    manager->subscribe("test.event", normal_listener);

    // Publish events - one should cause an exception
    manager->publish(docker_cpp::Event("test.event", "ok"));
    manager->publish(docker_cpp::Event("test.event", "throw"));
    manager->publish(docker_cpp::Event("test.event", "ok"));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Normal listener should receive all events
    // Faulty listener should handle the exception gracefully
    EXPECT_GE(received_events.size(), 2); // At least the events from normal listener
}

TEST_F(EventTest, EventManagerReset)
{
    auto* manager = docker_cpp::EventManager::getInstance();

    std::vector<docker_cpp::Event> received_events;
    docker_cpp::EventListener listener = [&](const docker_cpp::Event& event) {
        received_events.push_back(event);
    };

    manager->subscribe("test.event", listener);

    docker_cpp::Event event1("test.event", "Before reset");
    manager->publish(event1);

    EXPECT_EQ(received_events.size(), 1);

    docker_cpp::EventManager::resetInstance();

    // Get new instance
    auto* new_manager = docker_cpp::EventManager::getInstance();

    docker_cpp::Event event2("test.event", "After reset");
    new_manager->publish(event2);

    // Should not receive events in old listener
    EXPECT_EQ(received_events.size(), 1);
}
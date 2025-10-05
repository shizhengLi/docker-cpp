#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <docker-cpp/core/event.hpp>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace docker_cpp;

class EventSimpleTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // Reset event system before each test
        EventManager::resetInstance();
    }

    void TearDown() override
    {
        EventManager::resetInstance();
    }
};

// Test basic event creation and properties
TEST_F(EventSimpleTest, BasicEventCreation)
{
    Event event("test.event", "Test event data");

    EXPECT_EQ(event.getType(), "test.event");
    EXPECT_EQ(event.getData(), "Test event data");
    EXPECT_FALSE(event.getTimestamp().time_since_epoch().count() == 0);
    EXPECT_GT(event.getId(), 0);
}

TEST_F(EventSimpleTest, EventMetadata)
{
    Event event("test.event", "Test data");

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

TEST_F(EventSimpleTest, EventManagerSingleton)
{
    auto manager1 = EventManager::getInstance();
    auto manager2 = EventManager::getInstance();

    EXPECT_EQ(manager1, manager2);
}

TEST_F(EventSimpleTest, BasicEventPublishing)
{
    auto manager = EventManager::getInstance();

    std::atomic<int> received_count{0};
    EventListener listener = [&](const Event& event) {
        received_count++;
        EXPECT_EQ(event.getType(), "test.event");
        EXPECT_EQ(event.getData(), "Test data");
    };

    manager->subscribe("test.event", listener);

    Event event("test.event", "Test data");
    manager->publish(event);

    // Wait for async event processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_EQ(received_count, 1);
}

TEST_F(EventSimpleTest, MultipleSubscribers)
{
    auto manager = EventManager::getInstance();

    std::atomic<int> received1_count{0};
    std::atomic<int> received2_count{0};

    EventListener listener1 = [&](const Event& event) {
        received1_count++;
        EXPECT_EQ(event.getData(), "Test data");
    };

    EventListener listener2 = [&](const Event& event) {
        received2_count++;
        EXPECT_EQ(event.getData(), "Test data");
    };

    manager->subscribe("test.event", listener1);
    manager->subscribe("test.event", listener2);

    Event event("test.event", "Test data");
    manager->publish(event);

    // Wait for async event processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_EQ(received1_count, 1);
    EXPECT_EQ(received2_count, 1);
}

TEST_F(EventSimpleTest, EventFiltering)
{
    auto manager = EventManager::getInstance();

    std::atomic<int> received_count{0};
    EventListener listener = [&](const Event& event) {
        received_count++;
        // Should only receive test.event events
        EXPECT_EQ(event.getType(), "test.event");
    };

    manager->subscribe("test.event", listener);

    // Publish events of different types
    Event event1("test.event", "Should receive");
    Event event2("other.event", "Should not receive");
    Event event3("test.event", "Should also receive");

    manager->publish(event1);
    manager->publish(event2);
    manager->publish(event3);

    // Wait for async event processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_EQ(received_count, 2);
}

TEST_F(EventSimpleTest, UnsubscribeEvents)
{
    auto manager = EventManager::getInstance();

    std::atomic<int> received_count{0};
    EventListener listener = [&](const Event& event) {
        (void)event; // Suppress unused parameter warning
        received_count++;
    };

    SubscriptionId subscription = manager->subscribe("test.event", listener);

    Event event1("test.event", "Before unsubscribe");
    manager->publish(event1);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_EQ(received_count, 1);

    manager->unsubscribe(subscription);

    Event event2("test.event", "After unsubscribe");
    manager->publish(event2);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Should still be 1, not 2
    EXPECT_EQ(received_count, 1);
}

TEST_F(EventSimpleTest, EventStatistics)
{
    auto manager = EventManager::getInstance();

    std::atomic<int> received_count{0};
    EventListener listener = [&](const Event& event) {
        (void)event; // Suppress unused parameter warning
        received_count++;
    };

    manager->subscribe("test.event1", listener);
    manager->subscribe("test.event2", listener);

    // Publish events
    manager->publish(Event("test.event1", "Event 1"));
    manager->publish(Event("test.event1", "Event 2"));
    manager->publish(Event("test.event2", "Event 3"));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto stats = manager->getStatistics();

    EXPECT_EQ(stats.total_events_published, 3);
    EXPECT_EQ(stats.total_events_processed, 3);
    EXPECT_EQ(stats.active_subscriptions, 2);
    EXPECT_EQ(stats.pending_events, 0);
}
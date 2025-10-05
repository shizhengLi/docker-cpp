#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <regex>
#include <string>
#include <thread>
#include <unordered_map>
#include <variant>
#include <vector>

namespace docker_cpp {

using EventId = uint64_t;
using SubscriptionId = uint64_t;

// Event system constants
constexpr std::chrono::milliseconds DEFAULT_FLUSH_WAIT_TIME{10};
constexpr size_t DEFAULT_MAX_QUEUE_SIZE = 10000;
constexpr size_t DEFAULT_MAX_BATCH_SIZE = 100;

enum class EventPriority { LOW = 0, NORMAL = 1, HIGH = 2, CRITICAL = 3 };

class Event {
public:
    Event(std::string type,
          std::string data,
          const std::chrono::system_clock::time_point& timestamp = std::chrono::system_clock::now(),
          EventPriority priority = EventPriority::NORMAL);

    // Copy and move constructors
    Event(const Event& other) = default;
    Event(Event&& other) noexcept = default;
    Event& operator=(const Event& other) = default;
    Event& operator=(Event&& other) noexcept = default;

    // Accessors
    const std::string& getType() const
    {
        return type_;
    }
    const std::string& getData() const
    {
        return data_;
    }
    std::chrono::system_clock::time_point getTimestamp() const
    {
        return timestamp_;
    }
    EventId getId() const
    {
        return id_;
    }
    EventPriority getPriority() const
    {
        return priority_;
    }

    // Metadata operations
    template <typename T>
    void setMetadata(const std::string& key, const T& value);

    template <typename T>
    T getMetadata(const std::string& key) const;

    bool hasMetadata(const std::string& key) const;
    void removeMetadata(const std::string& key);

private:
    std::string type_;
    std::string data_;
    std::chrono::system_clock::time_point timestamp_;
    EventId id_;
    EventPriority priority_;
    std::unordered_map<std::string, std::variant<std::string, int, double, bool>> metadata_;

    static std::atomic<EventId> next_id_;
};

class EventManager;
using EventListener = std::function<void(const Event&)>;

struct EventStatistics {
    uint64_t total_events_published;
    uint64_t total_events_processed;
    uint64_t active_subscriptions;
    uint64_t pending_events;
};

class EventManager {
public:
    static EventManager* getInstance();
    static void resetInstance();

    ~EventManager();

    // Event publishing and subscription
    SubscriptionId subscribe(const std::string& event_type,
                             EventListener listener,
                             EventPriority priority = EventPriority::NORMAL);
    void unsubscribe(SubscriptionId subscription_id);
    void publish(const Event& event);

    // Batch processing
    void enableBatching(const std::string& event_type,
                        std::chrono::milliseconds batch_interval,
                        size_t max_batch_size = DEFAULT_MAX_BATCH_SIZE);
    void disableBatching(const std::string& event_type);

    // Statistics and monitoring
    EventStatistics getStatistics() const;
    void flush();

    // Configuration
    void setMaxQueueSize(size_t max_size);
    void setProcessingThreads(size_t num_threads);

    // Non-copyable, non-movable
    EventManager(const EventManager&) = delete;
    EventManager& operator=(const EventManager&) = delete;
    EventManager(EventManager&&) = delete;
    EventManager& operator=(EventManager&&) = delete;

private:
    EventManager() = default;

    struct Subscription {
        SubscriptionId id;
        std::string event_type_pattern;
        EventListener listener;
        EventPriority priority;
        bool active;
    };

    struct BatchConfig {
        std::chrono::milliseconds interval;
        size_t max_batch_size;
        bool enabled;
        std::vector<Event> pending_events;
        std::chrono::system_clock::time_point last_flush;
    };

    void processEventQueue();
    void processEvent(const Event& event);
    void processBatch(const std::string& event_type);
    void processBatchQueue();
    bool matchesPattern(const std::string& event_type, const std::string& pattern) const;
    void startProcessingThread();

    static std::unique_ptr<EventManager> instance_;
    static std::mutex instance_mutex_;

    mutable std::mutex subscriptions_mutex_;
    mutable std::mutex event_queue_mutex_;
    mutable std::mutex stats_mutex_;
    mutable std::mutex batches_mutex_;

    std::vector<Subscription> subscriptions_;
    std::priority_queue<Event, std::vector<Event>, std::function<bool(const Event&, const Event&)>>
        event_queue_;
    std::unordered_map<std::string, BatchConfig> batch_configs_;

    std::thread processing_thread_;
    std::thread batch_processing_thread_;
    std::condition_variable queue_condition_;
    std::atomic<bool> should_stop_{false};

    std::atomic<uint64_t> next_subscription_id_{1};
    EventStatistics stats_;
    size_t max_queue_size_ = DEFAULT_MAX_QUEUE_SIZE;

    friend class EventManagerTest; // For testing
};

// Template implementations
template <typename T>
void Event::setMetadata(const std::string& key, const T& value)
{
    metadata_[key] = value;
}

template <typename T>
T Event::getMetadata(const std::string& key) const
{
    auto it = metadata_.find(key);
    if (it == metadata_.end()) {
        throw std::runtime_error("Metadata key not found: " + key);
    }

    try {
        return std::get<T>(it->second);
    }
    catch (const std::bad_variant_access&) {
        throw std::runtime_error("Invalid type for metadata key: " + key);
    }
}

} // namespace docker_cpp
#include <algorithm>
#include <docker-cpp/core/event.hpp>
#include <iostream>
#include <stdexcept>

namespace docker_cpp {

// Static member definitions
std::atomic<EventId> Event::next_id_{1};

std::unique_ptr<EventManager> EventManager::instance_;
std::mutex EventManager::instance_mutex_;

// Event implementation
Event::Event(const std::string& type,
             const std::string& data,
             const std::chrono::system_clock::time_point& timestamp,
             EventPriority priority)
    : type_(type), data_(data), timestamp_(timestamp), id_(next_id_++), priority_(priority)
{}

bool Event::hasMetadata(const std::string& key) const
{
    return metadata_.find(key) != metadata_.end();
}

void Event::removeMetadata(const std::string& key)
{
    metadata_.erase(key);
}

// EventManager implementation
EventManager* EventManager::getInstance()
{
    std::lock_guard<std::mutex> lock(instance_mutex_);

    if (!instance_) {
        instance_ = std::unique_ptr<EventManager>(new EventManager());
        instance_->startProcessingThread();
    }

    return instance_.get();
}

void EventManager::resetInstance()
{
    std::lock_guard<std::mutex> lock(instance_mutex_);

    if (instance_) {
        instance_->should_stop_ = true;
        instance_->queue_condition_.notify_all();
        if (instance_->processing_thread_.joinable()) {
            instance_->processing_thread_.join();
        }
        instance_.reset();
    }
}

EventManager::~EventManager()
{
    should_stop_ = true;
    queue_condition_.notify_all();
    if (processing_thread_.joinable()) {
        processing_thread_.join();
    }
}

SubscriptionId EventManager::subscribe(const std::string& event_type_pattern,
                                       EventListener listener,
                                       EventPriority priority)
{
    std::lock_guard<std::mutex> lock(subscriptions_mutex_);

    Subscription subscription;
    subscription.id = next_subscription_id_++;
    subscription.event_type_pattern = event_type_pattern;
    subscription.listener = std::move(listener);
    subscription.priority = priority;
    subscription.active = true;

    subscriptions_.push_back(subscription);

    // Update statistics
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.active_subscriptions = subscriptions_.size();
    }

    return subscription.id;
}

void EventManager::unsubscribe(SubscriptionId subscription_id)
{
    std::lock_guard<std::mutex> lock(subscriptions_mutex_);

    auto it = std::find_if(
        subscriptions_.begin(), subscriptions_.end(), [subscription_id](const Subscription& sub) {
            return sub.id == subscription_id;
        });

    if (it != subscriptions_.end()) {
        it->active = false;
        subscriptions_.erase(it);

        // Update statistics
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.active_subscriptions = subscriptions_.size();
    }
}

void EventManager::publish(const Event& event)
{
    {
        std::lock_guard<std::mutex> lock(event_queue_mutex_);

        // Check queue size limit
        if (event_queue_.size() >= max_queue_size_) {
            std::cerr << "Event queue is full, dropping event: " << event.getType() << std::endl;
            return;
        }

        event_queue_.push(event);

        // Update statistics
        {
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.total_events_published++;
            stats_.pending_events = event_queue_.size();
        }
    }

    queue_condition_.notify_one();
}

void EventManager::enableBatching(const std::string& event_type,
                                  std::chrono::milliseconds batch_interval,
                                  size_t max_batch_size)
{
    std::lock_guard<std::mutex> lock(batches_mutex_);

    BatchConfig config;
    config.interval = batch_interval;
    config.max_batch_size = max_batch_size;
    config.enabled = true;
    config.last_flush = std::chrono::system_clock::now();

    batch_configs_[event_type] = std::move(config);
}

void EventManager::disableBatching(const std::string& event_type)
{
    std::lock_guard<std::mutex> lock(batches_mutex_);

    auto it = batch_configs_.find(event_type);
    if (it != batch_configs_.end()) {
        // Process any pending events in the batch
        if (!it->second.pending_events.empty()) {
            processBatch(event_type);
        }
        batch_configs_.erase(it);
    }
}

EventStatistics EventManager::getStatistics() const
{
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void EventManager::flush()
{
    // Process all pending events
    while (true) {
        {
            std::lock_guard<std::mutex> lock(event_queue_mutex_);
            if (event_queue_.empty()) {
                break;
            }
        }

        // Give some time for processing
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Process all batches
    {
        std::lock_guard<std::mutex> lock(batches_mutex_);
        for (auto& [event_type, config] : batch_configs_) {
            if (!config.pending_events.empty()) {
                processBatch(event_type);
            }
        }
    }
}

void EventManager::setMaxQueueSize(size_t max_size)
{
    max_queue_size_ = max_size;
}

void EventManager::setProcessingThreads(size_t num_threads)
{
    // For simplicity, we currently use one processing thread
    // This could be extended to use a thread pool
    (void)num_threads;
}

void EventManager::processEventQueue()
{
    while (!should_stop_) {
        std::unique_lock<std::mutex> lock(event_queue_mutex_);

        queue_condition_.wait(lock, [this] { return !event_queue_.empty() || should_stop_; });

        if (should_stop_) {
            break;
        }

        if (!event_queue_.empty()) {
            Event event = event_queue_.top();
            event_queue_.pop();

            lock.unlock();

            processEvent(event);

            // Update statistics
            {
                std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                stats_.total_events_processed++;
                stats_.pending_events = event_queue_.size();
            }
        }
    }
}

void EventManager::processEvent(const Event& event)
{
    // Check if batching is enabled for this event type
    {
        std::lock_guard<std::mutex> batches_lock(batches_mutex_);
        auto it = batch_configs_.find(event.getType());
        if (it != batch_configs_.end() && it->second.enabled) {
            // Add to batch
            it->second.pending_events.push_back(event);

            // Check if batch should be flushed
            auto now = std::chrono::system_clock::now();
            bool should_flush = false;

            if (it->second.pending_events.size() >= it->second.max_batch_size) {
                should_flush = true;
            }
            else if (now - it->second.last_flush >= it->second.interval) {
                should_flush = true;
            }

            if (should_flush) {
                processBatch(event.getType());
                it->second.last_flush = now;
            }
            return;
        }
    }

    // Process event directly (no batching)
    std::vector<Subscription> active_subscriptions;
    {
        std::lock_guard<std::mutex> lock(subscriptions_mutex_);

        for (const auto& subscription : subscriptions_) {
            if (subscription.active
                && matchesPattern(event.getType(), subscription.event_type_pattern)) {
                active_subscriptions.push_back(subscription);
            }
        }
    }

    // Notify subscribers
    for (const auto& subscription : active_subscriptions) {
        try {
            subscription.listener(event);
        }
        catch (const std::exception& e) {
            std::cerr << "Exception in event listener: " << e.what() << std::endl;
            // Continue processing other listeners
        }
        catch (...) {
            std::cerr << "Unknown exception in event listener" << std::endl;
            // Continue processing other listeners
        }
    }
}

void EventManager::processBatch(const std::string& event_type)
{
    std::vector<Event> events_to_process;

    {
        std::lock_guard<std::mutex> lock(batches_mutex_);
        auto it = batch_configs_.find(event_type);
        if (it != batch_configs_.end()) {
            events_to_process = std::move(it->second.pending_events);
            it->second.pending_events.clear();
        }
    }

    // Process all events in the batch
    for (const auto& event : events_to_process) {
        std::vector<Subscription> active_subscriptions;
        {
            std::lock_guard<std::mutex> lock(subscriptions_mutex_);

            for (const auto& subscription : subscriptions_) {
                if (subscription.active
                    && matchesPattern(event.getType(), subscription.event_type_pattern)) {
                    active_subscriptions.push_back(subscription);
                }
            }
        }

        // Notify subscribers
        for (const auto& subscription : active_subscriptions) {
            try {
                subscription.listener(event);
            }
            catch (const std::exception& e) {
                std::cerr << "Exception in event listener: " << e.what() << std::endl;
            }
            catch (...) {
                std::cerr << "Unknown exception in event listener" << std::endl;
            }
        }
    }
}

bool EventManager::matchesPattern(const std::string& event_type, const std::string& pattern) const
{
    if (pattern == "*") {
        return true; // Match all events
    }

    if (pattern.find('*') == std::string::npos) {
        return event_type == pattern; // Exact match
    }

    // Convert wildcard pattern to regex
    std::string regex_pattern = pattern;
    size_t pos = 0;
    while ((pos = regex_pattern.find('*', pos)) != std::string::npos) {
        regex_pattern.replace(pos, 1, ".*");
        pos += 2;
    }

    regex_pattern = "^" + regex_pattern + "$";

    try {
        std::regex re(regex_pattern);
        return std::regex_match(event_type, re);
    }
    catch (const std::regex_error&) {
        // If regex is invalid, fall back to exact match
        return event_type == pattern;
    }
}

void EventManager::startProcessingThread()
{
    // Initialize the priority queue with custom comparator
    event_queue_ = std::
        priority_queue<Event, std::vector<Event>, std::function<bool(const Event&, const Event&)>>(
            [](const Event& a, const Event& b) {
                // Higher priority events should be processed first
                return static_cast<int>(a.getPriority()) < static_cast<int>(b.getPriority());
            });

    // Initialize statistics
    stats_ = {0, 0, 0, 0};

    // Start processing thread
    processing_thread_ = std::thread(&EventManager::processEventQueue, this);
}

} // namespace docker_cpp
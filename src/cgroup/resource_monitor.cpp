#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include <docker-cpp/cgroup/cgroup_manager.hpp>

namespace docker_cpp {

// Resource monitor implementation
class ResourceMonitorImpl : public ResourceMonitor {
public:
    ResourceMonitorImpl()
        : monitoring_active_(false), monitoring_thread_(), monitored_cgroups_(), metrics_history_(),
          alert_callbacks_(), cpu_thresholds_(), memory_thresholds_(), io_thresholds_()
    {}

    ~ResourceMonitorImpl() override
    {
        stopMonitoringThread();
    }

    // Monitoring control
    void startMonitoring(const std::string& cgroup_path) override
    {
        std::lock_guard<std::mutex> lock(monitoring_mutex_);

        if (monitored_cgroups_.find(cgroup_path) == monitored_cgroups_.end()) {
            monitored_cgroups_[cgroup_path] = true;

            // Start monitoring thread if not already running
            if (!monitoring_active_) {
                startMonitoringThread();
            }
        }
    }

    void stopMonitoring(const std::string& cgroup_path) override
    {
        std::lock_guard<std::mutex> lock(monitoring_mutex_);

        auto it = monitored_cgroups_.find(cgroup_path);
        if (it != monitored_cgroups_.end()) {
            monitored_cgroups_.erase(it);

            // Stop monitoring thread if no cgroups are being monitored
            if (monitored_cgroups_.empty()) {
                stopMonitoringThread();
            }
        }
    }

    bool isMonitoring(const std::string& cgroup_path) const override
    {
        std::lock_guard<std::mutex> lock(monitoring_mutex_);
        return monitored_cgroups_.find(cgroup_path) != monitored_cgroups_.end();
    }

    // Metrics collection
    ResourceMetrics getCurrentMetrics(const std::string& cgroup_path) override
    {
        return getCurrentMetricsImpl(cgroup_path);
    }

    ResourceMetrics getCurrentMetricsImpl(const std::string& cgroup_path) const
    {
        // Create a temporary cgroup manager to get metrics
        CgroupConfig config;
        config.name = "temp_monitor";
        config.controllers = CgroupController::ALL;

        // Extract cgroup path from the full path
        std::string mount_point = CgroupManager::getMountPoint();
        if (cgroup_path.find(mount_point) == 0) {
            config.name = cgroup_path.substr(mount_point.length() + 1);
        }

        try {
            auto manager = CgroupManager::create(config);
            if (std::filesystem::exists(cgroup_path)) {
                return manager->getMetrics();
            }
        }
        catch (...) {
            // Ignore errors and return default metrics
        }

        // Return default metrics if cgroup doesn't exist or can't be accessed
        return ResourceMetrics{};
    }

    std::vector<ResourceMetrics> getHistoricalMetrics(const std::string& cgroup_path,
                                                      uint64_t start_time,
                                                      uint64_t end_time) override
    {
        std::lock_guard<std::mutex> lock(metrics_mutex_);

        std::vector<ResourceMetrics> result;
        auto it = metrics_history_.find(cgroup_path);

        if (it != metrics_history_.end()) {
            const auto& history = it->second;
            for (const auto& metrics : history) {
                if (metrics.timestamp >= start_time && metrics.timestamp <= end_time) {
                    result.push_back(metrics);
                }
            }
        }

        return result;
    }

    // Alerting
    void setCpuThreshold(const std::string& cgroup_path, double threshold_percent) override
    {
        std::lock_guard<std::mutex> lock(alerts_mutex_);
        cpu_thresholds_[cgroup_path] = threshold_percent;
    }

    void setMemoryThreshold(const std::string& cgroup_path, double threshold_percent) override
    {
        std::lock_guard<std::mutex> lock(alerts_mutex_);
        memory_thresholds_[cgroup_path] = threshold_percent;
    }

    void setIoThreshold(const std::string& cgroup_path, uint64_t bytes_per_second) override
    {
        std::lock_guard<std::mutex> lock(alerts_mutex_);
        io_thresholds_[cgroup_path] = bytes_per_second;
    }

    bool hasCpuAlert(const std::string& cgroup_path) const override
    {
        std::lock_guard<std::mutex> lock(alerts_mutex_);
        auto it = cpu_thresholds_.find(cgroup_path);
        if (it == cpu_thresholds_.end()) {
            return false;
        }

        try {
            ResourceMetrics metrics = getCurrentMetricsImpl(cgroup_path);
            return metrics.cpu.usage_percent > it->second;
        }
        catch (...) {
            return false;
        }
    }

    bool hasMemoryAlert(const std::string& cgroup_path) const override
    {
        std::lock_guard<std::mutex> lock(alerts_mutex_);
        auto it = memory_thresholds_.find(cgroup_path);
        if (it == memory_thresholds_.end()) {
            return false;
        }

        try {
            ResourceMetrics metrics = getCurrentMetricsImpl(cgroup_path);
            return metrics.memory.usage_percent > it->second;
        }
        catch (...) {
            return false;
        }
    }

    bool hasIoAlert(const std::string& cgroup_path) const override
    {
        std::lock_guard<std::mutex> lock(alerts_mutex_);
        auto it = io_thresholds_.find(cgroup_path);
        if (it == io_thresholds_.end()) {
            return false;
        }

        try {
            ResourceMetrics metrics = getCurrentMetricsImpl(cgroup_path);
            uint64_t total_io = metrics.io.rbytes + metrics.io.wbytes;

            // Simple threshold check (would need rate calculation for real implementation)
            return total_io > it->second;
        }
        catch (...) {
            return false;
        }
    }

    // Callbacks for events
    void setAlertCallback(AlertCallback callback) override
    {
        std::lock_guard<std::mutex> lock(alerts_mutex_);
        alert_callbacks_.push_back(callback);
    }

private:
    // Monitoring state
    mutable std::mutex monitoring_mutex_;
    std::atomic<bool> monitoring_active_;
    std::thread monitoring_thread_;
    std::unordered_map<std::string, bool> monitored_cgroups_;

    // Metrics storage
    mutable std::mutex metrics_mutex_;
    std::unordered_map<std::string, std::vector<ResourceMetrics>> metrics_history_;
    static constexpr size_t MAX_HISTORY_SIZE = 1000; // Keep last 1000 metrics per cgroup

    // Alerting
    mutable std::mutex alerts_mutex_;
    std::vector<AlertCallback> alert_callbacks_;
    std::unordered_map<std::string, double> cpu_thresholds_;
    std::unordered_map<std::string, double> memory_thresholds_;
    std::unordered_map<std::string, uint64_t> io_thresholds_;

    void startMonitoringThread()
    {
        if (monitoring_active_.load()) {
            return;
        }

        monitoring_active_.store(true);
        monitoring_thread_ = std::thread(&ResourceMonitorImpl::monitoringLoop, this);
    }

    void stopMonitoringThread()
    {
        if (!monitoring_active_.load()) {
            return;
        }

        monitoring_active_.store(false);
        if (monitoring_thread_.joinable()) {
            monitoring_thread_.join();
        }
    }

    void monitoringLoop()
    {
        while (monitoring_active_.load()) {
            collectMetrics();
            checkAlerts();

            // Sleep for monitoring interval (1 second)
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    void collectMetrics()
    {
        std::lock_guard<std::mutex> lock(monitoring_mutex_);

        for (const auto& [cgroup_path, _] : monitored_cgroups_) {
            try {
                ResourceMetrics metrics = getCurrentMetricsImpl(cgroup_path);

                // Store metrics in history
                {
                    std::lock_guard<std::mutex> metrics_lock(metrics_mutex_);
                    auto& history = metrics_history_[cgroup_path];
                    history.push_back(metrics);

                    // Limit history size
                    if (history.size() > MAX_HISTORY_SIZE) {
                        history.erase(history.begin());
                    }
                }
            }
            catch (...) {
                // Ignore errors for individual cgroups
            }
        }
    }

    void checkAlerts()
    {
        std::lock_guard<std::mutex> lock(monitoring_mutex_);

        for (const auto& [cgroup_path, _] : monitored_cgroups_) {
            try {
                ResourceMetrics metrics = getCurrentMetricsImpl(cgroup_path);

                // Check CPU alert
                {
                    std::lock_guard<std::mutex> alerts_lock(alerts_mutex_);
                    auto it = cpu_thresholds_.find(cgroup_path);
                    if (it != cpu_thresholds_.end() && metrics.cpu.usage_percent > it->second) {
                        triggerAlert(cgroup_path, "cpu", metrics.cpu.usage_percent);
                    }
                }

                // Check memory alert
                {
                    std::lock_guard<std::mutex> alerts_lock(alerts_mutex_);
                    auto it = memory_thresholds_.find(cgroup_path);
                    if (it != memory_thresholds_.end()
                        && metrics.memory.usage_percent > it->second) {
                        triggerAlert(cgroup_path, "memory", metrics.memory.usage_percent);
                    }
                }

                // Check IO alert
                {
                    std::lock_guard<std::mutex> alerts_lock(alerts_mutex_);
                    auto it = io_thresholds_.find(cgroup_path);
                    if (it != io_thresholds_.end()) {
                        uint64_t total_io = metrics.io.rbytes + metrics.io.wbytes;
                        if (total_io > it->second) {
                            triggerAlert(cgroup_path, "io", static_cast<double>(total_io));
                        }
                    }
                }
            }
            catch (...) {
                // Ignore errors for individual cgroups
            }
        }
    }

    void triggerAlert(const std::string& cgroup_path, const std::string& alert_type, double value)
    {
        std::lock_guard<std::mutex> lock(alerts_mutex_);

        for (const auto& callback : alert_callbacks_) {
            try {
                callback(cgroup_path, alert_type, value);
            }
            catch (...) {
                // Ignore callback errors
            }
        }
    }
};

// Factory method implementation
std::unique_ptr<ResourceMonitor> ResourceMonitor::create()
{
    return std::make_unique<ResourceMonitorImpl>();
}

} // namespace docker_cpp
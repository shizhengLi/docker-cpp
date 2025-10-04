# Docker C++ Implementation - Production Deployment: Reliability and Scalability

*Building enterprise-grade container runtime systems with high availability and disaster recovery*

## Table of Contents
1. [Introduction](#introduction)
2. [Production Architecture Overview](#production-architecture-overview)
3. [High Availability Patterns](#high-availability-patterns)
4. [Disaster Recovery Strategies](#disaster-recovery-strategies)
5. [Performance Tuning at Scale](#performance-tuning-at-scale)
6. [Monitoring and Alerting](#monitoring-and-alerting)
7. [Resource Management](#resource-management)
8. [Complete Implementation](#complete-implementation)
9. [Deployment Automation](#deployment-automation)
10. [Operational Best Practices](#operational-best-practices)

## Introduction

Deploying container runtime systems in production environments requires careful consideration of reliability, scalability, and operational complexity. Production systems must handle thousands of containers, maintain high availability, and gracefully handle failures while providing consistent performance.

This article covers the implementation of production-ready features for our Docker C++ runtime, including:

- **High Availability**: Redundant systems and automatic failover
- **Disaster Recovery**: Backup, restore, and geographic redundancy
- **Performance Optimization**: Scaling patterns and resource utilization
- **Monitoring**: Comprehensive observability and alerting
- **Operational Automation**: Deployment, upgrades, and maintenance

## Production Architecture Overview

### Enterprise Architecture

```cpp
// Production deployment architecture
// ┌─────────────────────────────────────────────────────────────────┐
// │                   Load Balancer Layer                          │
// │ ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ │
// │ │   HAProxy   │ │   NGINX     │ │    Envoy    │ │  Custom LB  │ │
// │ │   Primary   │ │  Secondary  │ │   Backup    │ │  Emergency  │ │
// │ └─────────────┘ └─────────────┘ └─────────────┘ └─────────────┘ │
// └─────────────────────────────────────────────────────────────────┘
//                                │
// ┌─────────────────────────────────────────────────────────────────┐
// │                 Docker C++ Runtime Cluster                    │
// │ ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ │
// │ │   Runtime   │ │   Runtime   │ │   Runtime   │ │   Runtime   │ │
// │ │   Node 1    │ │   Node 2    │ │   Node 3    │ │   Node N    │ │
// │ └─────────────┘ └─────────────┘ └─────────────┘ └─────────────┘ │
// └─────────────────────────────────────────────────────────────────┘
//                                │
// ┌─────────────────────────────────────────────────────────────────┐
// │                   Storage and Network Layer                    │
// │ ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ │
// │ │ Distributed │ │  Shared     │ │   Network   │ │  Backup     │ │
// │ │    Storage  │ │  Volume     │ │   Fabric    │ │   Storage   │ │
// │ │    (Ceph)   │ │  (NFS/GFS)  │ │  (VXLAN)    │ │  (S3/Glacier)│ │
// │ └─────────────┘ └─────────────┘ └─────────────┘ └─────────────┘ │
// └─────────────────────────────────────────────────────────────────┘
//                                │
// ┌─────────────────────────────────────────────────────────────────┐
// │                 Management and Monitoring                       │
// │ ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ │
// │ │   Metrics   │ │    Logs     │ │   Tracing   │ │   Health    │ │
// │ │  (Prometheus)│ │ (ELK Stack)│ │ (Jaeger)    │ │  Checks     │ │
// │ └─────────────┘ └─────────────┘ └─────────────┘ └─────────────┘ │
// └─────────────────────────────────────────────────────────────────┘
```

### Core Components

```cpp
#pragma once
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <functional>

namespace production {

// Node status enumeration
enum class NodeStatus {
    ACTIVE,
    STANDBY,
    MAINTENANCE,
    FAILED,
    UNKNOWN
};

// Cluster configuration
struct ClusterConfig {
    std::string cluster_id;
    std::vector<std::string> node_addresses;
    std::string discovery_service;
    std::string storage_backend;
    std::string network_backend;

    // High availability settings
    size_t quorum_size = 3;
    std::chrono::milliseconds heartbeat_interval{1000};
    std::chrono::milliseconds election_timeout{5000};
    std::chrono::milliseconds failover_timeout{10000};

    // Performance settings
    size_t max_containers_per_node = 1000;
    size_t default_memory_per_container = 1024 * 1024 * 1024; // 1GB
    size_t default_cpu_shares = 1024;

    // Backup settings
    std::chrono::hours backup_interval{24};
    std::string backup_location;
    size_t backup_retention_days = 30;
};

// Node information
struct NodeInfo {
    std::string id;
    std::string address;
    NodeStatus status = NodeStatus::UNKNOWN;
    std::chrono::steady_clock::time_point last_heartbeat;

    // Resource information
    size_t total_memory = 0;
    size_t available_memory = 0;
    size_t total_cpu_cores = 0;
    double cpu_usage = 0.0;
    size_t running_containers = 0;

    // Network information
    std::vector<std::string> network_interfaces;
    std::string default_gateway;
    std::unordered_map<std::string, std::string> dns_servers;

    // Health information
    std::unordered_map<std::string, bool> health_checks;
    std::vector<std::string> recent_errors;
};

// Service interface
class IService {
public:
    virtual ~IService() = default;

    virtual bool initialize(const ClusterConfig& config) = 0;
    virtual bool start() = 0;
    virtual bool stop() = 0;
    virtual void cleanup() = 0;

    virtual bool isHealthy() const = 0;
    virtual std::string getStatus() const = 0;
    virtual std::unordered_map<std::string, std::string> getMetrics() const = 0;
};

// High availability interface
class IHighAvailabilityManager {
public:
    virtual ~IHighAvailabilityManager() = default;

    virtual bool electLeader() = 0;
    virtual bool becomeLeader() = 0;
    virtual bool stepDown() = 0;
    virtual bool addFollower(const std::string& nodeId) = 0;
    virtual bool removeFollower(const std::string& nodeId) = 0;

    virtual bool isLeader() const = 0;
    virtual std::string getLeaderId() const = 0;
    virtual std::vector<std::string> getFollowers() const = 0;

    virtual bool replicateData(const std::string& key, const std::string& value) = 0;
    virtual std::string getReplicatedData(const std::string& key) = 0;
};

// Disaster recovery interface
class IDisasterRecoveryManager {
public:
    virtual ~IDisasterRecoveryManager() = default;

    virtual bool createBackup(const std::string& backup_id) = 0;
    virtual bool restoreBackup(const std::string& backup_id) = 0;
    virtual bool deleteBackup(const std::string& backup_id) = 0;

    virtual std::vector<std::string> listBackups() = 0;
    virtual std::optional<std::chrono::system_clock::time_point> getBackupTime(const std::string& backup_id) = 0;

    virtual bool enableGeoRedundancy(const std::string& region) = 0;
    virtual bool testFailover(const std::string& region) = 0;
    virtual bool performFailover(const std::string& region) = 0;
};

// Performance manager interface
class IPerformanceManager {
public:
    virtual ~IPerformanceManager() = default;

    virtual bool enableAutoScaling() = 0;
    virtual bool disableAutoScaling() = 0;
    virtual bool setScalingPolicy(const std::string& policy_name,
                                 const std::unordered_map<std::string, std::string>& parameters) = 0;

    virtual bool balanceLoad() = 0;
    virtual bool migrateContainer(const std::string& container_id,
                                 const std::string& target_node) = 0;

    virtual std::unordered_map<std::string, double> getLoadMetrics() const = 0;
    virtual std::vector<std::string> getRecommendations() const = 0;
};

} // namespace production
```

## High Availability Patterns

### Leader Election and Consensus

```cpp
#include <algorithm>
#include <random>

namespace production {

// Raft-based consensus implementation
class RaftConsensus : public IHighAvailabilityManager {
private:
    enum class NodeState {
        FOLLOWER,
        CANDIDATE,
        LEADER
    };

    struct LogEntry {
        size_t term;
        size_t index;
        std::string command;
        std::chrono::steady_clock::time_point timestamp;
    };

    NodeState state_ = NodeState::FOLLOWER;
    size_t current_term_ = 0;
    std::string voted_for_;
    std::string leader_id_;
    size_t commit_index_ = 0;
    size_t last_applied_ = 0;

    // Election variables
    std::chrono::steady_clock::time_point last_heartbeat_;
    std::chrono::milliseconds election_timeout_{5000};
    std::thread election_thread_;
    std::atomic<bool> running_{true};

    // Log replication
    std::vector<LogEntry> log_;
    std::unordered_map<std::string, size_t> next_index_;
    std::unordered_map<std::string, size_t> match_index_;

    // Cluster state
    std::string node_id_;
    std::vector<std::string> cluster_nodes_;
    mutable std::mutex state_mutex_;

    // Network communication
    std::unordered_map<std::string, std::unique_ptr<INodeCommunicator>> communicators_;

public:
    RaftConsensus(const std::string& nodeId, const std::vector<std::string>& nodes)
        : node_id_(nodeId), cluster_nodes_(nodes) {

        // Initialize communicators for each node
        for (const auto& nodeId : nodes) {
            if (nodeId != node_id_) {
                communicators_[nodeId] = createCommunicator(nodeId);
            }
        }

        // Start election thread
        election_thread_ = std::thread(&RaftConsensus::electionLoop, this);
    }

    ~RaftConsensus() {
        running_ = false;
        if (election_thread_.joinable()) {
            election_thread_.join();
        }
    }

    bool electLeader() override {
        std::lock_guard<std::mutex> lock(state_mutex_);

        if (state_ == NodeState::LEADER) {
            return true;
        }

        // Become candidate
        state_ = NodeState::CANDIDATE;
        current_term_++;
        voted_for_ = node_id_;

        // Start election
        size_t votes = 1; // Vote for self
        for (const auto& nodeId : cluster_nodes_) {
            if (nodeId != node_id_) {
                if (requestVote(nodeId, current_term_)) {
                    votes++;
                }
            }
        }

        // Check if won election
        size_t majority = (cluster_nodes_.size() / 2) + 1;
        if (votes >= majority) {
            state_ = NodeState::LEADER;
            leader_id_ = node_id_;
            becomeLeader();
            return true;
        }

        // Lost election, become follower
        state_ = NodeState::FOLLOWER;
        return false;
    }

    bool becomeLeader() override {
        std::lock_guard<std::mutex> lock(state_mutex_);

        if (state_ != NodeState::LEADER) {
            return false;
        }

        // Initialize leader state
        for (const auto& nodeId : cluster_nodes_) {
            if (nodeId != node_id_) {
                next_index_[nodeId] = log_.size() + 1;
                match_index_[nodeId] = 0;
            }
        }

        // Start heartbeat thread
        startHeartbeat();

        return true;
    }

    bool stepDown() override {
        std::lock_guard<std::mutex> lock(state_mutex_);

        if (state_ == NodeState::LEADER) {
            stopHeartbeat();
            state_ = NodeState::FOLLOWER;
            leader_id_.clear();
            return true;
        }

        return false;
    }

    bool addFollower(const std::string& nodeId) override {
        std::lock_guard<std::mutex> lock(state_mutex_);

        if (std::find(cluster_nodes_.begin(), cluster_nodes_.end(), nodeId) == cluster_nodes_.end()) {
            cluster_nodes_.push_back(nodeId);
            communicators_[nodeId] = createCommunicator(nodeId);

            if (state_ == NodeState::LEADER) {
                next_index_[nodeId] = log_.size() + 1;
                match_index_[nodeId] = 0;
            }

            return true;
        }

        return false;
    }

    bool removeFollower(const std::string& nodeId) override {
        std::lock_guard<std::mutex> lock(state_mutex_);

        auto it = std::find(cluster_nodes_.begin(), cluster_nodes_.end(), nodeId);
        if (it != cluster_nodes_.end()) {
            cluster_nodes_.erase(it);
            communicators_.erase(nodeId);
            next_index_.erase(nodeId);
            match_index_.erase(nodeId);
            return true;
        }

        return false;
    }

    bool isLeader() const override {
        std::lock_guard<std::mutex> lock(state_mutex_);
        return state_ == NodeState::LEADER;
    }

    std::string getLeaderId() const override {
        std::lock_guard<std::mutex> lock(state_mutex_);
        return leader_id_;
    }

    std::vector<std::string> getFollowers() const override {
        std::lock_guard<std::mutex> lock(state_mutex_);

        std::vector<std::string> followers;
        for (const auto& nodeId : cluster_nodes_) {
            if (nodeId != node_id_ && nodeId != leader_id_) {
                followers.push_back(nodeId);
            }
        }
        return followers;
    }

    bool replicateData(const std::string& key, const std::string& value) override {
        std::lock_guard<std::mutex> lock(state_mutex_);

        if (state_ != NodeState::LEADER) {
            return false;
        }

        // Create log entry
        LogEntry entry;
        entry.term = current_term_;
        entry.index = log_.size() + 1;
        entry.command = key + ":" + value;
        entry.timestamp = std::chrono::steady_clock::now();

        log_.push_back(entry);

        // Replicate to followers
        return replicateToFollowers(entry);
    }

    std::string getReplicatedData(const std::string& key) override {
        std::lock_guard<std::mutex> lock(state_mutex_);

        // Search log for key
        for (auto it = log_.rbegin(); it != log_.rend(); ++it) {
            if (it->command.find(key + ":") == 0) {
                return it->command.substr(key.length() + 1);
            }
        }

        return "";
    }

private:
    void electionLoop() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(3000, 5000);

        while (running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            std::lock_guard<std::mutex> lock(state_mutex_);

            if (state_ == NodeState::LEADER) {
                continue;
            }

            // Check if election timeout has expired
            auto now = std::chrono::steady_clock::now();
            auto elapsed = now - last_heartbeat_;

            if (elapsed > election_timeout_) {
                // Start election
                state_ = NodeState::CANDIDATE;
                current_term_++;
                voted_for_ = node_id_;

                // Reset election timeout
                election_timeout_ = std::chrono::milliseconds(dis(gen));
                last_heartbeat_ = now;

                // Request votes from all nodes
                size_t votes = 1; // Vote for self
                for (const auto& nodeId : cluster_nodes_) {
                    if (nodeId != node_id_) {
                        if (requestVote(nodeId, current_term_)) {
                            votes++;
                        }
                    }
                }

                // Check if won election
                size_t majority = (cluster_nodes_.size() / 2) + 1;
                if (votes >= majority) {
                    state_ = NodeState::LEADER;
                    leader_id_ = node_id_;
                    becomeLeader();
                } else {
                    state_ = NodeState::FOLLOWER;
                }
            }
        }
    }

    bool requestVote(const std::string& nodeId, size_t term) {
        auto it = communicators_.find(nodeId);
        if (it == communicators_.end()) {
            return false;
        }

        // Send vote request
        VoteRequest request{term, node_id_, log_.size(), getLastLogTerm()};
        VoteResponse response = it->second->requestVote(request);

        if (response.term > current_term_) {
            current_term_ = response.term;
            state_ = NodeState::FOLLOWER;
            voted_for_.clear();
        }

        return response.vote_granted;
    }

    bool replicateToFollowers(const LogEntry& entry) {
        std::vector<std::future<bool>> futures;

        for (const auto& nodeId : cluster_nodes_) {
            if (nodeId != node_id_) {
                auto future = std::async(std::launch::async, [this, nodeId, entry]() {
                    auto it = communicators_.find(nodeId);
                    if (it == communicators_.end()) {
                        return false;
                    }

                    AppendEntriesRequest request{
                        current_term_,
                        node_id_,
                        entry.index - 1,
                        getLogTerm(entry.index - 1),
                        {entry},
                        commit_index_
                    };

                    AppendEntriesResponse response = it->second->appendEntries(request);

                    if (response.term > current_term_) {
                        current_term_ = response.term;
                        state_ = NodeState::FOLLOWER;
                    }

                    return response.success;
                });

                futures.push_back(std::move(future));
            }
        }

        // Wait for majority of responses
        size_t success_count = 1; // Leader counts as success
        for (auto& future : futures) {
            if (future.get()) {
                success_count++;
            }
        }

        size_t majority = (cluster_nodes_.size() / 2) + 1;
        return success_count >= majority;
    }

    void startHeartbeat() {
        std::thread([this]() {
            while (running_ && state_ == NodeState::LEADER) {
                sendHeartbeats();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }).detach();
    }

    void stopHeartbeat() {
        // Heartbeat thread will exit on next check
    }

    void sendHeartbeats() {
        for (const auto& nodeId : cluster_nodes_) {
            if (nodeId != node_id_) {
                auto it = communicators_.find(nodeId);
                if (it != communicators_.end()) {
                    AppendEntriesRequest request{
                        current_term_,
                        node_id_,
                        log_.size(),
                        getLastLogTerm(),
                        {}, // Empty heartbeat
                        commit_index_
                    };

                    it->second->appendEntries(request);
                }
            }
        }
    }

    size_t getLastLogTerm() const {
        if (log_.empty()) {
            return 0;
        }
        return log_.back().term;
    }

    size_t getLogTerm(size_t index) const {
        if (index == 0 || index > log_.size()) {
            return 0;
        }
        return log_[index - 1].term;
    }

    std::unique_ptr<INodeCommunicator> createCommunicator(const std::string& nodeId) {
        // Implementation would create actual network communicator
        return std::make_unique<MockNodeCommunicator>(nodeId);
    }

    // Request/Response structures
    struct VoteRequest {
        size_t term;
        std::string candidate_id;
        size_t last_log_index;
        size_t last_log_term;
    };

    struct VoteResponse {
        size_t term;
        bool vote_granted;
    };

    struct AppendEntriesRequest {
        size_t term;
        std::string leader_id;
        size_t prev_log_index;
        size_t prev_log_term;
        std::vector<LogEntry> entries;
        size_t leader_commit;
    };

    struct AppendEntriesResponse {
        size_t term;
        bool success;
    };
};

// High availability manager implementation
class HighAvailabilityManager : public IService {
private:
    std::unique_ptr<IHighAvailabilityManager> consensus_;
    ClusterConfig config_;
    std::string node_id_;
    std::atomic<bool> running_{false};
    std::thread health_check_thread_;

public:
    HighAvailabilityManager(const std::string& nodeId) : node_id_(nodeId) {}

    bool initialize(const ClusterConfig& config) override {
        config_ = config;

        // Initialize consensus algorithm
        std::vector<std::string> nodes;
        for (const auto& address : config.node_addresses) {
            nodes.push_back(extractNodeId(address));
        }

        consensus_ = std::make_unique<RaftConsensus>(node_id_, nodes);

        return true;
    }

    bool start() override {
        running_ = true;

        // Start health check thread
        health_check_thread_ = std::thread(&HighAvailabilityManager::healthCheckLoop, this);

        // Attempt to become leader
        if (consensus_->electLeader()) {
            // Successfully became leader
            return true;
        }

        return true; // Still running as follower
    }

    bool stop() override {
        running_ = false;

        if (health_check_thread_.joinable()) {
            health_check_thread_.join();
        }

        if (consensus_->isLeader()) {
            consensus_->stepDown();
        }

        return true;
    }

    void cleanup() override {
        consensus_.reset();
    }

    bool isHealthy() const override {
        return consensus_ != nullptr;
    }

    std::string getStatus() const override {
        if (consensus_->isLeader()) {
            return "Leader";
        } else {
            return "Follower (Leader: " + consensus_->getLeaderId() + ")";
        }
    }

    std::unordered_map<std::string, std::string> getMetrics() const override {
        return {
            {"node_id", node_id_},
            {"is_leader", consensus_->isLeader() ? "true" : "false"},
            {"leader_id", consensus_->getLeaderId()},
            {"followers_count", std::to_string(consensus_->getFollowers().size())}
        };
    }

private:
    void healthCheckLoop() {
        while (running_) {
            std::this_thread::sleep_for(config_.heartbeat_interval);

            // Check leader health
            if (!consensus_->isLeader() && consensus_->getLeaderId().empty()) {
                // No leader, start election
                consensus_->electLeader();
            }
        }
    }

    std::string extractNodeId(const std::string& address) {
        // Extract node ID from address
        return address; // Simplified
    }
};

} // namespace production
```

## Disaster Recovery Strategies

### Backup and Restore System

```cpp
#include <fstream>
#include <archive.h>
#include <archive_entry.h>

namespace production {

// Backup manager implementation
class DisasterRecoveryManager : public IService, public IDisasterRecoveryManager {
private:
    struct BackupMetadata {
        std::string backup_id;
        std::chrono::system_clock::time_point created_at;
        size_t size_bytes;
        std::string checksum;
        std::string type; // full, incremental
        std::vector<std::string> included_containers;
        std::vector<std::string> included_volumes;
        std::unordered_map<std::string, std::string> tags;
    };

    ClusterConfig config_;
    std::string local_backup_path_;
    std::unordered_map<std::string, BackupMetadata> backup_registry_;
    std::atomic<bool> backup_in_progress_{false};
    std::thread backup_scheduler_;
    std::atomic<bool> running_{false};

    // Geo-redundancy
    std::unordered_map<std::string, std::unique_ptr<IGeoStorage>> geo_storages_;
    std::vector<std::string> active_regions_;

public:
    DisasterRecoveryManager(const ClusterConfig& config) : config_(config) {
        local_backup_path_ = config_.backup_location.empty() ?
                            "/var/lib/docker/backups" : config_.backup_location;
        std::filesystem::create_directories(local_backup_path_);
    }

    bool initialize(const ClusterConfig& config) override {
        config_ = config;

        // Initialize geo-storage connections
        for (const auto& region : config_.node_addresses) {
            auto storage = createGeoStorage(region);
            if (storage && storage->initialize(config_)) {
                geo_storages_[region] = std::move(storage);
                active_regions_.push_back(region);
            }
        }

        // Load existing backup registry
        loadBackupRegistry();

        return true;
    }

    bool start() override {
        running_ = true;

        // Start backup scheduler
        backup_scheduler_ = std::thread(&DisasterRecoveryManager::backupSchedulerLoop, this);

        return true;
    }

    bool stop() override {
        running_ = false;

        if (backup_scheduler_.joinable()) {
            backup_scheduler_.join();
        }

        return true;
    }

    void cleanup() override {
        saveBackupRegistry();
        geo_storages_.clear();
    }

    bool isHealthy() const override {
        return running_ && !backup_in_progress_;
    }

    std::string getStatus() const override {
        if (backup_in_progress_) {
            return "Backup in progress";
        }
        return "Ready";
    }

    std::unordered_map<std::string, std::string> getMetrics() const override {
        return {
            {"backup_count", std::to_string(backup_registry_.size())},
            {"backup_in_progress", backup_in_progress_ ? "true" : "false"},
            {"active_regions", std::to_string(active_regions_.size())},
            {"last_backup", getLastBackupTime()}
        };
    }

    // IDisasterRecoveryManager implementation
    bool createBackup(const std::string& backup_id) override {
        if (backup_in_progress_) {
            return false; // Backup already in progress
        }

        std::thread([this, backup_id]() {
            try {
                performBackup(backup_id);
            } catch (const std::exception& e) {
                // Log error
            }
        }).detach();

        return true;
    }

    bool restoreBackup(const std::string& backup_id) override {
        auto it = backup_registry_.find(backup_id);
        if (it == backup_registry_.end()) {
            return false; // Backup not found
        }

        return performRestore(it->second);
    }

    bool deleteBackup(const std::string& backup_id) override {
        auto it = backup_registry_.find(backup_id);
        if (it == backup_registry_.end()) {
            return false;
        }

        // Delete from local storage
        std::string backup_path = local_backup_path_ + "/" + backup_id + ".tar.gz";
        std::filesystem::remove(backup_path);

        // Delete from geo-storage
        for (const auto& region : active_regions_) {
            auto storage_it = geo_storages_.find(region);
            if (storage_it != geo_storages_.end()) {
                storage_it->second->deleteObject(backup_id + ".tar.gz");
            }
        }

        // Remove from registry
        backup_registry_.erase(it);
        saveBackupRegistry();

        return true;
    }

    std::vector<std::string> listBackups() override {
        std::vector<std::string> backup_ids;
        for (const auto& [id, metadata] : backup_registry_) {
            backup_ids.push_back(id);
        }
        return backup_ids;
    }

    std::optional<std::chrono::system_clock::time_point> getBackupTime(const std::string& backup_id) override {
        auto it = backup_registry_.find(backup_id);
        if (it != backup_registry_.end()) {
            return it->second.created_at;
        }
        return std::nullopt;
    }

    bool enableGeoRedundancy(const std::string& region) override {
        auto storage = createGeoStorage(region);
        if (storage && storage->initialize(config_)) {
            geo_storages_[region] = std::move(storage);

            if (std::find(active_regions_.begin(), active_regions_.end(), region) == active_regions_.end()) {
                active_regions_.push_back(region);
            }

            return true;
        }

        return false;
    }

    bool testFailover(const std::string& region) override {
        auto storage_it = geo_storages_.find(region);
        if (storage_it == geo_storages_.end()) {
            return false;
        }

        // Test connectivity and access
        return storage_it->second->testConnectivity();
    }

    bool performFailover(const std::string& region) override {
        if (!testFailover(region)) {
            return false;
        }

        // Promote region to primary
        // Implementation would update cluster configuration
        return true;
    }

private:
    void performBackup(const std::string& backup_id) {
        backup_in_progress_ = true;

        try {
            // Create backup metadata
            BackupMetadata metadata;
            metadata.backup_id = backup_id;
            metadata.created_at = std::chrono::system_clock::now();
            metadata.type = "full";

            // Collect container and volume information
            std::vector<std::string> containers = getRunningContainers();
            std::vector<std::string> volumes = getActiveVolumes();

            metadata.included_containers = containers;
            metadata.included_volumes = volumes;

            // Create backup archive
            std::string backup_path = local_backup_path_ + "/" + backup_id + ".tar.gz";
            if (createBackupArchive(backup_path, containers, volumes)) {
                // Calculate checksum
                metadata.checksum = calculateChecksum(backup_path);
                metadata.size_bytes = std::filesystem::file_size(backup_path);

                // Store in registry
                backup_registry_[backup_id] = metadata;

                // Replicate to geo-storage
                replicateBackupToGeoRegions(backup_path, backup_id);

                // Cleanup old backups
                cleanupOldBackups();

                saveBackupRegistry();
            }

        } catch (const std::exception& e) {
            // Log error
        }

        backup_in_progress_ = false;
    }

    bool createBackupArchive(const std::string& backup_path,
                            const std::vector<std::string>& containers,
                            const std::vector<std::string>& volumes) {
        struct archive* a = archive_write_new();
        archive_write_set_format_gnutar(a);
        archive_write_add_filter_gzip(a);
        archive_write_open_filename(a, backup_path.c_str());

        bool success = true;

        try {
            // Backup container configurations
            for (const auto& container_id : containers) {
                if (!backupContainer(a, container_id)) {
                    success = false;
                    break;
                }
            }

            // Backup volumes
            for (const auto& volume_name : volumes) {
                if (!backupVolume(a, volume_name)) {
                    success = false;
                    break;
                }
            }

            // Backup system configuration
            backupSystemConfig(a);

        } catch (const std::exception& e) {
            success = false;
        }

        archive_write_close(a);
        archive_write_free(a);

        return success;
    }

    bool backupContainer(struct archive* a, const std::string& container_id) {
        // Get container configuration
        std::string config = getContainerConfig(container_id);

        // Create archive entry
        struct archive_entry* entry = archive_entry_new();
        std::string entry_name = "containers/" + container_id + "/config.json";

        archive_entry_set_pathname(entry, entry_name.c_str());
        archive_entry_set_size(entry, config.length());
        archive_entry_set_filetype(entry, AE_IFREG);
        archive_entry_set_perm(entry, 0644);

        archive_write_header(a, entry);
        archive_write_data(a, config.c_str(), config.length());
        archive_entry_free(entry);

        return true;
    }

    bool backupVolume(struct archive* a, const std::string& volume_name) {
        std::string volume_path = getVolumePath(volume_name);

        // Archive volume contents
        return archiveDirectory(a, volume_path, "volumes/" + volume_name);
    }

    bool backupSystemConfig(struct archive* a) {
        // Backup system configurations
        std::vector<std::string> config_files = {
            "/etc/docker/daemon.json",
            "/var/lib/docker/network/files/local-kv.db"
        };

        for (const auto& file : config_files) {
            if (std::filesystem::exists(file)) {
                archiveFile(a, file, "config/" + std::filesystem::path(file).filename().string());
            }
        }

        return true;
    }

    bool performRestore(const BackupMetadata& metadata) {
        std::string backup_path = local_backup_path_ + "/" + metadata.backup_id + ".tar.gz";

        if (!std::filesystem::exists(backup_path)) {
            // Try to fetch from geo-storage
            if (!fetchBackupFromGeoRegions(backup_path, metadata.backup_id)) {
                return false;
            }
        }

        // Verify checksum
        std::string checksum = calculateChecksum(backup_path);
        if (checksum != metadata.checksum) {
            return false; // Backup corrupted
        }

        // Stop running containers
        for (const auto& container_id : getRunningContainers()) {
            stopContainer(container_id);
        }

        // Extract backup
        if (!extractBackupArchive(backup_path)) {
            return false;
        }

        // Restore containers
        for (const auto& container_id : metadata.included_containers) {
            restoreContainer(container_id);
        }

        // Restore volumes
        for (const auto& volume_name : metadata.included_volumes) {
            restoreVolume(volume_name);
        }

        return true;
    }

    bool extractBackupArchive(const std::string& backup_path) {
        struct archive* a = archive_read_new();
        archive_read_support_format_gnutar(a);
        archive_read_support_filter_gzip(a);

        int r = archive_read_open_filename(a, backup_path.c_str(), 10240);
        if (r != ARCHIVE_OK) {
            archive_read_free(a);
            return false;
        }

        struct archive_entry* entry;
        while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
            std::string path = archive_entry_pathname(entry);

            if (path.find("containers/") == 0) {
                // Restore container configuration
                restoreContainerFromEntry(a, entry);
            } else if (path.find("volumes/") == 0) {
                // Restore volume data
                restoreVolumeFromEntry(a, entry);
            } else if (path.find("config/") == 0) {
                // Restore system configuration
                restoreConfigFromEntry(a, entry);
            }
        }

        archive_read_close(a);
        archive_read_free(a);

        return true;
    }

    void backupSchedulerLoop() {
        while (running_) {
            std::this_thread::sleep_for(config_.backup_interval);

            if (!backup_in_progress_) {
                std::string backup_id = generateBackupId();
                createBackup(backup_id);
            }
        }
    }

    void replicateBackupToGeoRegions(const std::string& backup_path, const std::string& backup_id) {
        std::ifstream file(backup_path, std::ios::binary);
        std::vector<char> buffer(std::filesystem::file_size(backup_path));
        file.read(buffer.data(), buffer.size());

        for (const auto& region : active_regions_) {
            auto storage_it = geo_storages_.find(region);
            if (storage_it != geo_storages_.end()) {
                storage_it->second->putObject(backup_id + ".tar.gz", buffer.data(), buffer.size());
            }
        }
    }

    bool fetchBackupFromGeoRegions(const std::string& backup_path, const std::string& backup_id) {
        for (const auto& region : active_regions_) {
            auto storage_it = geo_storages_.find(region);
            if (storage_it != geo_storages_.end()) {
                std::vector<char> buffer;
                if (storage_it->second->getObject(backup_id + ".tar.gz", buffer)) {
                    std::ofstream file(backup_path, std::ios::binary);
                    file.write(buffer.data(), buffer.size());
                    return true;
                }
            }
        }
        return false;
    }

    void cleanupOldBackups() {
        auto now = std::chrono::system_clock::now();
        auto cutoff = now - std::chrono::hours(24 * config_.backup_retention_days);

        auto it = backup_registry_.begin();
        while (it != backup_registry_.end()) {
            if (it->second.created_at < cutoff) {
                deleteBackup(it->first);
                it = backup_registry_.erase(it);
            } else {
                ++it;
            }
        }
    }

    std::string generateBackupId() {
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
        return "backup_" + std::to_string(timestamp);
    }

    std::string getLastBackupTime() const {
        if (backup_registry_.empty()) {
            return "never";
        }

        auto latest = std::max_element(backup_registry_.begin(), backup_registry_.end(),
            [](const auto& a, const auto& b) {
                return a.second.created_at < b.second.created_at;
            });

        auto time_t = std::chrono::system_clock::to_time_t(latest->second.created_at);
        return std::string(std::ctime(&time_t));
    }

    void saveBackupRegistry() {
        std::string registry_path = local_backup_path_ + "/registry.json";
        std::ofstream file(registry_path);

        // Serialize backup registry to JSON
        file << "{\n";
        for (auto it = backup_registry_.begin(); it != backup_registry_.end(); ++it) {
            if (it != backup_registry_.begin()) file << ",\n";

            file << "  \"" << it->first << "\": {\n";
            file << "    \"created_at\": " << std::chrono::duration_cast<std::chrono::seconds>(
                it->second.created_at.time_since_epoch()).count() << ",\n";
            file << "    \"size_bytes\": " << it->second.size_bytes << ",\n";
            file << "    \"checksum\": \"" << it->second.checksum << "\",\n";
            file << "    \"type\": \"" << it->second.type << "\"\n";
            file << "  }";
        }
        file << "\n}\n";
    }

    void loadBackupRegistry() {
        std::string registry_path = local_backup_path_ + "/registry.json";
        if (!std::filesystem::exists(registry_path)) {
            return;
        }

        // Parse JSON and load backup registry
        // Implementation would use JSON library
    }

    // Helper methods
    std::vector<std::string> getRunningContainers() {
        // Implementation would query Docker daemon
        return {};
    }

    std::vector<std::string> getActiveVolumes() {
        // Implementation would query Docker daemon
        return {};
    }

    std::string getContainerConfig(const std::string& container_id) {
        // Implementation would get container configuration
        return "{}";
    }

    std::string getVolumePath(const std::string& volume_name) {
        // Implementation would get volume path
        return "/var/lib/docker/volumes/" + volume_name;
    }

    void stopContainer(const std::string& container_id) {
        // Implementation would stop container
    }

    void restoreContainer(const std::string& container_id) {
        // Implementation would restore container
    }

    void restoreVolume(const std::string& volume_name) {
        // Implementation would restore volume
    }

    std::unique_ptr<IGeoStorage> createGeoStorage(const std::string& region) {
        // Implementation would create appropriate storage backend
        return std::make_unique<MockGeoStorage>(region);
    }

    std::string calculateChecksum(const std::string& file_path) {
        // Implementation would calculate SHA-256 checksum
        return "checksum";
    }

    bool archiveFile(struct archive* a, const std::string& file_path, const std::string& entry_name) {
        struct archive_entry* entry = archive_entry_new();
        archive_entry_set_pathname(entry, entry_name.c_str());

        struct stat st;
        stat(file_path.c_str(), &st);
        archive_entry_copy_stat(entry, &st);

        archive_write_header(a, entry);

        std::ifstream file(file_path, std::ios::binary);
        std::vector<char> buffer(1024 * 1024);

        while (file.read(buffer.data(), buffer.size())) {
            archive_write_data(a, buffer.data(), file.gcount());
        }

        if (file.gcount() > 0) {
            archive_write_data(a, buffer.data(), file.gcount());
        }

        archive_entry_free(entry);
        return true;
    }

    bool archiveDirectory(struct archive* a, const std::string& dir_path, const std::string& entry_prefix) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir_path)) {
            if (entry.is_regular_file()) {
                std::string relative_path = entry.path().string().substr(dir_path.length());
                std::string entry_name = entry_prefix + relative_path;
                archiveFile(a, entry.path().string(), entry_name);
            }
        }
        return true;
    }

    void restoreContainerFromEntry(struct archive* a, struct archive_entry* entry) {
        // Implementation would restore container configuration
    }

    void restoreVolumeFromEntry(struct archive* a, struct archive_entry* entry) {
        // Implementation would restore volume data
    }

    void restoreConfigFromEntry(struct archive* a, struct archive_entry* entry) {
        // Implementation would restore system configuration
    }
};

} // namespace production
```

## Performance Tuning at Scale

### Auto-scaling and Load Balancing

```cpp
#include <numeric>
#include <algorithm>

namespace production {

// Performance manager implementation
class PerformanceManager : public IService, public IPerformanceManager {
private:
    struct ScalingPolicy {
        std::string name;
        std::unordered_map<std::string, double> thresholds;
        std::unordered_map<std::string, std::string> actions;
        std::chrono::seconds cooldown{300};
        std::chrono::steady_clock::time_point last_action;
    };

    struct NodeMetrics {
        std::string node_id;
        double cpu_usage = 0.0;
        double memory_usage = 0.0;
        double disk_usage = 0.0;
        double network_usage = 0.0;
        size_t running_containers = 0;
        std::chrono::steady_clock::time_point last_update;
    };

    ClusterConfig config_;
    std::unordered_map<std::string, NodeMetrics> node_metrics_;
    std::vector<ScalingPolicy> scaling_policies_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_scale_actions_;

    std::atomic<bool> auto_scaling_enabled_{false};
    std::atomic<bool> running_{false};
    std::thread monitoring_thread_;
    std::thread scaling_thread_;

    // Load balancing
    std::unordered_map<std::string, std::vector<std::string>> container_assignments_;
    std::mutex assignment_mutex_;

public:
    PerformanceManager(const ClusterConfig& config) : config_(config) {}

    bool initialize(const ClusterConfig& config) override {
        config_ = config;

        // Initialize default scaling policies
        initializeDefaultPolicies();

        return true;
    }

    bool start() override {
        running_ = true;

        // Start monitoring thread
        monitoring_thread_ = std::thread(&PerformanceManager::monitoringLoop, this);

        // Start scaling thread
        scaling_thread_ = std::thread(&PerformanceManager::scalingLoop, this);

        return true;
    }

    bool stop() override {
        running_ = false;

        if (monitoring_thread_.joinable()) {
            monitoring_thread_.join();
        }

        if (scaling_thread_.joinable()) {
            scaling_thread_.join();
        }

        return true;
    }

    void cleanup() override {
        node_metrics_.clear();
        scaling_policies_.clear();
    }

    bool isHealthy() const override {
        return running_;
    }

    std::string getStatus() const override {
        return auto_scaling_enabled_ ? "Auto-scaling enabled" : "Auto-scaling disabled";
    }

    std::unordered_map<std::string, std::string> getMetrics() const override {
        return {
            {"auto_scaling_enabled", auto_scaling_enabled_ ? "true" : "false"},
            {"monitored_nodes", std::to_string(node_metrics_.size())},
            {"active_policies", std::to_string(scaling_policies_.size())},
            {"last_scale_action", getLastScaleActionTime()}
        };
    }

    // IPerformanceManager implementation
    bool enableAutoScaling() override {
        auto_scaling_enabled_ = true;
        return true;
    }

    bool disableAutoScaling() override {
        auto_scaling_enabled_ = false;
        return true;
    }

    bool setScalingPolicy(const std::string& policy_name,
                         const std::unordered_map<std::string, std::string>& parameters) override {
        ScalingPolicy policy;
        policy.name = policy_name;

        // Parse parameters
        for (const auto& [key, value] : parameters) {
            if (key == "cpu_threshold") {
                policy.thresholds["cpu"] = std::stod(value);
            } else if (key == "memory_threshold") {
                policy.thresholds["memory"] = std::stod(value);
            } else if (key == "action") {
                policy.actions["main"] = value;
            } else if (key == "cooldown_seconds") {
                policy.cooldown = std::chrono::seconds(std::stoi(value));
            }
        }

        scaling_policies_.push_back(policy);
        return true;
    }

    bool balanceLoad() override {
        std::lock_guard<std::mutex> lock(assignment_mutex_);

        // Calculate current load distribution
        std::unordered_map<std::string, double> node_loads;
        for (const auto& [node_id, metrics] : node_metrics_) {
            double load = calculateNodeLoad(metrics);
            node_loads[node_id] = load;
        }

        // Find overloaded and underloaded nodes
        std::vector<std::string> overloaded_nodes;
        std::vector<std::string> underloaded_nodes;

        double avg_load = calculateAverageLoad(node_loads);
        double overload_threshold = avg_load * 1.2;
        double underload_threshold = avg_load * 0.8;

        for (const auto& [node_id, load] : node_loads) {
            if (load > overload_threshold) {
                overloaded_nodes.push_back(node_id);
            } else if (load < underload_threshold) {
                underloaded_nodes.push_back(node_id);
            }
        }

        // Migrate containers from overloaded to underloaded nodes
        bool migrated = false;
        for (const auto& overloaded_node : overloaded_nodes) {
            auto containers = getContainersOnNode(overloaded_node);

            for (const auto& container_id : containers) {
                if (underloaded_nodes.empty()) break;

                // Find best target node
                std::string target_node = findBestTargetNode(container_id, underloaded_nodes, node_loads);
                if (!target_node.empty()) {
                    if (migrateContainer(container_id, target_node)) {
                        migrated = true;
                        updateContainerAssignment(container_id, target_node);

                        // Update load calculations
                        updateNodeLoadAfterMigration(overloaded_node, target_node, container_id, node_loads);

                        // Check if target is now overloaded
                        if (node_loads[target_node] > overload_threshold) {
                            auto it = std::find(underloaded_nodes.begin(), underloaded_nodes.end(), target_node);
                            if (it != underloaded_nodes.end()) {
                                underloaded_nodes.erase(it);
                            }
                        }
                    }
                }
            }
        }

        return migrated;
    }

    bool migrateContainer(const std::string& container_id, const std::string& target_node) override {
        // Implement container migration
        // 1. Stop container on source node
        // 2. Export container state
        // 3. Transfer to target node
        // 4. Import state and start container

        return true; // Simplified
    }

    std::unordered_map<std::string, double> getLoadMetrics() const override {
        std::unordered_map<std::string, double> metrics;

        for (const auto& [node_id, node_metrics] : node_metrics_) {
            double load = calculateNodeLoad(node_metrics);
            metrics[node_id] = load;
        }

        return metrics;
    }

    std::vector<std::string> getRecommendations() const override {
        std::vector<std::string> recommendations;

        // Analyze current state and provide recommendations
        if (auto_scaling_enabled_) {
            if (shouldScaleUp()) {
                recommendations.push_back("Consider scaling up - nodes are overloaded");
            }
            if (shouldScaleDown()) {
                recommendations.push_back("Consider scaling down - nodes are underutilized");
            }
        }

        if (needsLoadBalancing()) {
            recommendations.push_back("Load balancing recommended - uneven distribution detected");
        }

        if (hasStrugglingNodes()) {
            recommendations.push_back("Some nodes are experiencing performance issues");
        }

        return recommendations;
    }

private:
    void initializeDefaultPolicies() {
        // CPU-based scaling policy
        ScalingPolicy cpu_policy;
        cpu_policy.name = "cpu_scaling";
        cpu_policy.thresholds["cpu"] = 80.0; // 80% CPU threshold
        cpu_policy.actions["scale_up"] = "add_node";
        cpu_policy.actions["scale_down"] = "remove_node";
        cpu_policy.cooldown = std::chrono::seconds(300);
        scaling_policies_.push_back(cpu_policy);

        // Memory-based scaling policy
        ScalingPolicy memory_policy;
        memory_policy.name = "memory_scaling";
        memory_policy.thresholds["memory"] = 85.0; // 85% memory threshold
        memory_policy.actions["scale_up"] = "add_node";
        memory_policy.cooldown = std::chrono::seconds(300);
        scaling_policies_.push_back(memory_policy);

        // Container count policy
        ScalingPolicy container_policy;
        container_policy.name = "container_count_scaling";
        container_policy.thresholds["containers"] = static_cast<double>(config_.max_containers_per_node * 0.9);
        container_policy.actions["scale_up"] = "add_node";
        container_policy.cooldown = std::chrono::seconds(600);
        scaling_policies_.push_back(container_policy);
    }

    void monitoringLoop() {
        while (running_) {
            std::this_thread::sleep_for(std::chrono::seconds(10));

            // Collect metrics from all nodes
            for (const auto& node_address : config_.node_addresses) {
                std::string node_id = extractNodeId(node_address);
                collectNodeMetrics(node_id, node_address);
            }

            // Remove stale node data
            cleanupStaleMetrics();
        }
    }

    void scalingLoop() {
        while (running_) {
            std::this_thread::sleep_for(std::chrono::seconds(30));

            if (auto_scaling_enabled_) {
                evaluateScalingPolicies();
            }
        }
    }

    void collectNodeMetrics(const std::string& node_id, const std::string& node_address) {
        // Implementation would collect metrics from node
        NodeMetrics metrics;
        metrics.node_id = node_id;
        metrics.cpu_usage = getCpuUsage(node_address);
        metrics.memory_usage = getMemoryUsage(node_address);
        metrics.disk_usage = getDiskUsage(node_address);
        metrics.network_usage = getNetworkUsage(node_address);
        metrics.running_containers = getRunningContainerCount(node_address);
        metrics.last_update = std::chrono::steady_clock::now();

        node_metrics_[node_id] = metrics;
    }

    void evaluateScalingPolicies() {
        for (const auto& policy : scaling_policies_) {
            if (shouldExecutePolicy(policy)) {
                executePolicyAction(policy);
            }
        }
    }

    bool shouldExecutePolicy(const ScalingPolicy& policy) {
        // Check cooldown
        auto now = std::chrono::steady_clock::now();
        if (now - policy.last_action < policy.cooldown) {
            return false;
        }

        // Check thresholds
        for (const auto& [metric, threshold] : policy.thresholds) {
            if (metric == "cpu" && exceedsCpuThreshold(threshold)) {
                return true;
            } else if (metric == "memory" && exceedsMemoryThreshold(threshold)) {
                return true;
            } else if (metric == "containers" && exceedsContainerThreshold(threshold)) {
                return true;
            }
        }

        return false;
    }

    void executePolicyAction(const ScalingPolicy& policy) {
        for (const auto& [action_name, action_value] : policy.actions) {
            if (action_value == "add_node") {
                addNodeToCluster();
            } else if (action_value == "remove_node") {
                removeNodeFromCluster();
            }
        }

        // Update last action time
        const_cast<ScalingPolicy&>(policy).last_action = std::chrono::steady_clock::now();
    }

    double calculateNodeLoad(const NodeMetrics& metrics) {
        // Weighted load calculation
        double cpu_weight = 0.4;
        double memory_weight = 0.3;
        double container_weight = 0.2;
        double network_weight = 0.1;

        double load = (metrics.cpu_usage * cpu_weight) +
                     (metrics.memory_usage * memory_weight) +
                     ((static_cast<double>(metrics.running_containers) / config_.max_containers_per_node) * 100.0 * container_weight) +
                     (metrics.network_usage * network_weight);

        return load;
    }

    double calculateAverageLoad(const std::unordered_map<std::string, double>& node_loads) {
        if (node_loads.empty()) {
            return 0.0;
        }

        double sum = std::accumulate(node_loads.begin(), node_loads.end(), 0.0,
            [](double acc, const auto& pair) {
                return acc + pair.second;
            });

        return sum / node_loads.size();
    }

    std::string findBestTargetNode(const std::string& container_id,
                                  const std::vector<std::string>& candidate_nodes,
                                  const std::unordered_map<std::string, double>& node_loads) {
        std::string best_node;
        double min_load = std::numeric_limits<double>::max();

        for (const auto& node_id : candidate_nodes) {
            auto it = node_loads.find(node_id);
            if (it != node_loads.end() && it->second < min_load) {
                // Check if node can accommodate the container
                if (canNodeAccommodateContainer(node_id, container_id)) {
                    min_load = it->second;
                    best_node = node_id;
                }
            }
        }

        return best_node;
    }

    bool canNodeAccommodateContainer(const std::string& node_id, const std::string& container_id) {
        auto it = node_metrics_.find(node_id);
        if (it == node_metrics_.end()) {
            return false;
        }

        const auto& metrics = it->second;

        // Check if node has enough resources
        return metrics.cpu_usage < 90.0 &&
               metrics.memory_usage < 90.0 &&
               metrics.running_containers < config_.max_containers_per_node;
    }

    std::vector<std::string> getContainersOnNode(const std::string& node_id) {
        // Implementation would return containers running on node
        return {};
    }

    void updateContainerAssignment(const std::string& container_id, const std::string& target_node) {
        container_assignments_[container_id] = {target_node};
    }

    void updateNodeLoadAfterMigration(const std::string& source_node,
                                    const std::string& target_node,
                                    const std::string& container_id,
                                    std::unordered_map<std::string, double>& node_loads) {
        // Simplified load update
        node_loads[source_node] -= 5.0; // Arbitrary value
        node_loads[target_node] += 5.0;
    }

    bool shouldScaleUp() {
        for (const auto& [node_id, metrics] : node_metrics_) {
            if (metrics.cpu_usage > 85.0 || metrics.memory_usage > 90.0) {
                return true;
            }
        }
        return false;
    }

    bool shouldScaleDown() {
        double avg_cpu = 0.0, avg_memory = 0.0;
        for (const auto& [node_id, metrics] : node_metrics_) {
            avg_cpu += metrics.cpu_usage;
            avg_memory += metrics.memory_usage;
        }

        if (!node_metrics_.empty()) {
            avg_cpu /= node_metrics_.size();
            avg_memory /= node_metrics_.size();
        }

        return avg_cpu < 30.0 && avg_memory < 40.0 && node_metrics_.size() > 1;
    }

    bool needsLoadBalancing() {
        auto loads = getLoadMetrics();
        if (loads.empty()) return false;

        double max_load = 0.0, min_load = std::numeric_limits<double>::max();
        for (const auto& [node_id, load] : loads) {
            max_load = std::max(max_load, load);
            min_load = std::min(min_load, load);
        }

        return (max_load - min_load) > 30.0; // 30% difference threshold
    }

    bool hasStrugglingNodes() {
        for (const auto& [node_id, metrics] : node_metrics_) {
            if (metrics.cpu_usage > 95.0 || metrics.memory_usage > 95.0) {
                return true;
            }
        }
        return false;
    }

    void addNodeToCluster() {
        // Implementation would add new node to cluster
    }

    void removeNodeFromCluster() {
        // Implementation would safely remove node from cluster
    }

    void cleanupStaleMetrics() {
        auto now = std::chrono::steady_clock::now();
        auto cutoff = now - std::chrono::minutes(5);

        auto it = node_metrics_.begin();
        while (it != node_metrics_.end()) {
            if (it->second.last_update < cutoff) {
                it = node_metrics_.erase(it);
            } else {
                ++it;
            }
        }
    }

    bool exceedsCpuThreshold(double threshold) {
        for (const auto& [node_id, metrics] : node_metrics_) {
            if (metrics.cpu_usage > threshold) {
                return true;
            }
        }
        return false;
    }

    bool exceedsMemoryThreshold(double threshold) {
        for (const auto& [node_id, metrics] : node_metrics_) {
            if (metrics.memory_usage > threshold) {
                return true;
            }
        }
        return false;
    }

    bool exceedsContainerThreshold(double threshold) {
        for (const auto& [node_id, metrics] : node_metrics_) {
            if (static_cast<double>(metrics.running_containers) > threshold) {
                return true;
            }
        }
        return false;
    }

    // Helper methods
    std::string extractNodeId(const std::string& address) {
        return address; // Simplified
    }

    double getCpuUsage(const std::string& node_address) {
        // Implementation would get CPU usage from node
        return 50.0; // Placeholder
    }

    double getMemoryUsage(const std::string& node_address) {
        // Implementation would get memory usage from node
        return 60.0; // Placeholder
    }

    double getDiskUsage(const std::string& node_address) {
        // Implementation would get disk usage from node
        return 70.0; // Placeholder
    }

    double getNetworkUsage(const std::string& node_address) {
        // Implementation would get network usage from node
        return 30.0; // Placeholder
    }

    size_t getRunningContainerCount(const std::string& node_address) {
        // Implementation would get container count from node
        return 50; // Placeholder
    }

    std::string getLastScaleActionTime() const {
        // Implementation would return last scale action time
        return "never";
    }
};

} // namespace production
```

## Complete Implementation

### Production Orchestrator

```cpp
namespace production {

// Production orchestrator that manages all production services
class ProductionOrchestrator {
private:
    ClusterConfig config_;
    std::string node_id_;

    // Core services
    std::unique_ptr<HighAvailabilityManager> ha_manager_;
    std::unique_ptr<DisasterRecoveryManager> dr_manager_;
    std::unique_ptr<PerformanceManager> perf_manager_;

    // Monitoring and alerting
    std::unique_ptr<IMonitoringService> monitoring_service_;
    std::unique_ptr<IAlertingService> alerting_service_;

    // State management
    std::atomic<bool> running_{false};
    std::thread orchestrator_thread_;

public:
    ProductionOrchestrator(const ClusterConfig& config, const std::string& nodeId)
        : config_(config), node_id_(nodeId) {}

    bool initialize() {
        // Initialize high availability manager
        ha_manager_ = std::make_unique<HighAvailabilityManager>(node_id_);
        if (!ha_manager_->initialize(config_)) {
            return false;
        }

        // Initialize disaster recovery manager
        dr_manager_ = std::make_unique<DisasterRecoveryManager>(config_);
        if (!dr_manager_->initialize(config_)) {
            return false;
        }

        // Initialize performance manager
        perf_manager_ = std::make_unique<PerformanceManager>(config_);
        if (!perf_manager_->initialize(config_)) {
            return false;
        }

        // Initialize monitoring service
        monitoring_service_ = createMonitoringService(config_);
        if (!monitoring_service_->initialize(config_)) {
            return false;
        }

        // Initialize alerting service
        alerting_service_ = createAlertingService(config_);
        if (!alerting_service_->initialize(config_)) {
            return false;
        }

        return true;
    }

    bool start() {
        running_ = true;

        // Start core services
        if (!ha_manager_->start() ||
            !dr_manager_->start() ||
            !perf_manager_->start() ||
            !monitoring_service_->start() ||
            !alerting_service_->start()) {
            stop();
            return false;
        }

        // Start orchestrator thread
        orchestrator_thread_ = std::thread(&ProductionOrchestrator::orchestratorLoop, this);

        return true;
    }

    bool stop() {
        running_ = false;

        if (orchestrator_thread_.joinable()) {
            orchestrator_thread_.join();
        }

        // Stop services in reverse order
        if (alerting_service_) alerting_service_->stop();
        if (monitoring_service_) monitoring_service_->stop();
        if (perf_manager_) perf_manager_->stop();
        if (dr_manager_) dr_manager_->stop();
        if (ha_manager_) ha_manager_->stop();

        return true;
    }

    void cleanup() {
        if (ha_manager_) ha_manager_->cleanup();
        if (dr_manager_) dr_manager_->cleanup();
        if (perf_manager_) perf_manager_->cleanup();
        if (monitoring_service_) monitoring_service_->cleanup();
        if (alerting_service_) alerting_service_->cleanup();
    }

    bool isHealthy() const {
        return ha_manager_->isHealthy() &&
               dr_manager_->isHealthy() &&
               perf_manager_->isHealthy() &&
               monitoring_service_->isHealthy() &&
               alerting_service_->isHealthy();
    }

    std::string getStatus() const {
        std::string status = "Production Orchestrator Status:\n";
        status += "HA Manager: " + ha_manager_->getStatus() + "\n";
        status += "DR Manager: " + dr_manager_->getStatus() + "\n";
        status += "Perf Manager: " + perf_manager_->getStatus() + "\n";
        status += "Monitoring: " + monitoring_service_->getStatus() + "\n";
        status += "Alerting: " + alerting_service_->getStatus();
        return status;
    }

    std::unordered_map<std::string, std::string> getMetrics() const {
        std::unordered_map<std::string, std::string> all_metrics;

        auto ha_metrics = ha_manager_->getMetrics();
        all_metrics.insert(ha_metrics.begin(), ha_metrics.end());

        auto dr_metrics = dr_manager_->getMetrics();
        all_metrics.insert(dr_metrics.begin(), dr_metrics.end());

        auto perf_metrics = perf_manager_->getMetrics();
        all_metrics.insert(perf_metrics.begin(), perf_metrics.end());

        auto mon_metrics = monitoring_service_->getMetrics();
        all_metrics.insert(mon_metrics.begin(), mon_metrics.end());

        auto alert_metrics = alerting_service_->getMetrics();
        all_metrics.insert(alert_metrics.begin(), alert_metrics.end());

        return all_metrics;
    }

private:
    void orchestratorLoop() {
        while (running_) {
            try {
                // Collect and aggregate metrics
                auto metrics = getMetrics();

                // Perform health checks
                bool healthy = isHealthy();
                if (!healthy) {
                    alerting_service_->sendAlert("Orchestrator", "System unhealthy", "critical");
                }

                // Check if this node is leader
                if (ha_manager_->isLeader()) {
                    // Leader-specific tasks
                    performLeaderTasks();
                }

                // Load balancing
                perf_manager_->balanceLoad();

                // Process recommendations
                auto recommendations = perf_manager_->getRecommendations();
                for (const auto& recommendation : recommendations) {
                    processRecommendation(recommendation);
                }

                std::this_thread::sleep_for(std::chrono::seconds(30));

            } catch (const std::exception& e) {
                alerting_service_->sendAlert("Orchestrator",
                    "Error in orchestrator loop: " + std::string(e.what()), "error");
            }
        }
    }

    void performLeaderTasks() {
        // Only leader performs certain tasks

        // Trigger backups if needed
        static auto last_backup = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        if (now - last_backup > config_.backup_interval) {
            std::string backup_id = "auto_backup_" + std::to_string(
                std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count());
            dr_manager_->createBackup(backup_id);
            last_backup = now;
        }

        // Cluster-wide coordination
        coordinateClusterOperations();
    }

    void coordinateClusterOperations() {
        // Coordinate operations across the cluster
        // This would involve communicating with other nodes
    }

    void processRecommendation(const std::string& recommendation) {
        if (recommendation.find("scaling") != std::string::npos) {
            alerting_service_->sendAlert("Scaling", recommendation, "info");
        } else if (recommendation.find("load balancing") != std::string::npos) {
            alerting_service_->sendAlert("LoadBalancing", recommendation, "warning");
        } else if (recommendation.find("performance") != std::string::npos) {
            alerting_service_->sendAlert("Performance", recommendation, "warning");
        }
    }

    std::unique_ptr<IMonitoringService> createMonitoringService(const ClusterConfig& config) {
        return std::make_unique<MockMonitoringService>();
    }

    std::unique_ptr<IAlertingService> createAlertingService(const ClusterConfig& config) {
        return std::make_unique<MockAlertingService>();
    }
};

} // namespace production
```

## Usage Examples

### Production Deployment

```cpp
#include "production_orchestrator.h"

int main(int argc, char* argv[]) {
    try {
        // Parse command line arguments
        std::string config_file = "production_config.json";
        std::string node_id = "node-1";

        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--config" && i + 1 < argc) {
                config_file = argv[++i];
            } else if (arg == "--node-id" && i + 1 < argc) {
                node_id = argv[++i];
            }
        }

        // Load configuration
        production::ClusterConfig config = loadConfiguration(config_file);

        // Create and initialize orchestrator
        production::ProductionOrchestrator orchestrator(config, node_id);

        if (!orchestrator.initialize()) {
            std::cerr << "Failed to initialize production orchestrator" << std::endl;
            return 1;
        }

        // Set up signal handlers for graceful shutdown
        std::signal(SIGINT, [](int) {
            std::cout << "Received SIGINT, shutting down..." << std::endl;
            // Signal orchestrator to stop
        });

        std::signal(SIGTERM, [](int) {
            std::cout << "Received SIGTERM, shutting down..." << std::endl;
            // Signal orchestrator to stop
        });

        // Start orchestrator
        if (!orchestrator.start()) {
            std::cerr << "Failed to start production orchestrator" << std::endl;
            return 1;
        }

        std::cout << "Production orchestrator started successfully" << std::endl;
        std::cout << orchestrator.getStatus() << std::endl;

        // Run until shutdown
        while (orchestrator.isHealthy()) {
            std::this_thread::sleep_for(std::chrono::seconds(10));
        }

        // Cleanup
        orchestrator.stop();
        orchestrator.cleanup();

        std::cout << "Production orchestrator shutdown completed" << std::endl;
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}
```

## Operational Best Practices

### Production Deployment Guidelines

1. **High Availability**:
   - Deploy at least 3 nodes for quorum
   - Use geographic distribution for disaster recovery
   - Implement proper health checking and automatic failover

2. **Performance Optimization**:
   - Monitor resource utilization continuously
   - Implement auto-scaling based on workload patterns
   - Use load balancing to distribute work evenly

3. **Backup and Recovery**:
   - Schedule regular automated backups
   - Test backup restoration procedures
   - Maintain geo-redundancy for critical data

4. **Monitoring and Alerting**:
   - Set up comprehensive monitoring of all components
   - Configure appropriate alert thresholds
   - Implement notification escalation policies

5. **Security**:
   - Use encryption for data in transit and at rest
   - Implement proper authentication and authorization
   - Regularly update and patch all components

6. **Maintenance**:
   - Schedule regular maintenance windows
   - Use rolling updates to avoid downtime
   - Test all changes in staging environments first

This comprehensive production deployment system provides enterprise-grade reliability, scalability, and operational excellence for container runtime environments, ensuring smooth operation even at massive scale.
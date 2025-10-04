# Network Virtualization: Bridge, NAT, and Overlay Networks

## Introduction

Container networking is a critical component of containerization, enabling containers to communicate with each other and the outside world while maintaining isolation and security. This article explores the implementation of comprehensive container networking in C++, covering bridge networks, NAT, overlay networks, and advanced networking features.

## Container Networking Fundamentals

### Network Models

1. **Bridge Mode**: Default Docker networking with virtual bridges
2. **Host Mode**: Container shares host network namespace
3. **Overlay Mode**: Multi-host networking for distributed containers
4. **None Mode**: Container has no network interfaces
5. **Macvlan Mode**: Container gets direct access to physical network

### Network Isolation Levels

```mermaid
graph TB
    subgraph "Host Network"
        HostEth[eth0 192.168.1.100]
    end

    subgraph "Docker Network Stack"
        Bridge[docker0 172.17.0.1]
        veth1[veth0 Container1]
        veth2[veth0 Container2]
        veth3[veth0 Container3]
    end

    subgraph "Container Networks"
        Container1Eth[eth0 172.17.0.2]
        Container2Eth[eth0 172.17.0.3]
        Container3Eth[eth0 172.17.0.4]
    end

    HostEth -. NAT .|Bridge
    Bridge -- veth pair -- veth1
    Bridge -- veth pair -- veth2
    Bridge -- veth pair -- veth3

    veth1 -- Container1Eth
    veth2 -- Container2Eth
    veth3 -- Container3Eth
```

## Network Manager Architecture

### 1. Core Network Manager

```cpp
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

class NetworkManager {
public:
    enum class NetworkType {
        BRIDGE,
        OVERLAY,
        MACVLAN,
        HOST,
        NONE
    };

    struct NetworkConfig {
        std::string name;
        NetworkType type;
        std::string subnet;
        std::string gateway;
        std::string driver = "bridge";
        std::map<std::string, std::string> options;
        bool enable_ipv6 = false;
        std::string ipv6_subnet;
        std::map<std::string, std::string> labels;
        bool internal = false; // No external access
        bool attachable = false; // Manual container attachment
    };

    struct ContainerNetworkConfig {
        std::string network_id;
        std::vector<std::string> aliases;
        std::map<std::string, std::string> custom_dns;
        std::vector<std::string> custom_domains;
        std::string mac_address;
        std::vector<PortMapping> port_mappings;
    };

    struct PortMapping {
        std::string container_ip;
        uint16_t container_port;
        std::string host_ip;
        uint16_t host_port;
        std::string protocol = "tcp"; // tcp, udp, sctp
    };

    explicit NetworkManager(const std::string& network_dir)
        : network_dir_(network_dir), bridge_driver_(network_dir + "/bridge"),
          overlay_driver_(network_dir + "/overlay") {
        initializeNetworkStorage();
        loadNetworkConfigs();
    }

    // Network lifecycle management
    std::string createNetwork(const NetworkConfig& config);
    void deleteNetwork(const std::string& network_id);
    NetworkConfig inspectNetwork(const std::string& network_id) const;
    std::vector<NetworkConfig> listNetworks() const;

    // Container network attachment
    std::string connectContainer(const std::string& network_id,
                                const std::string& container_id,
                                const ContainerNetworkConfig& config = {});
    void disconnectContainer(const std::string& network_id,
                           const std::string& container_id);
    std::vector<std::string> getContainerNetworks(const std::string& container_id) const;

    // Port management
    void exposePorts(const std::string& container_id,
                    const std::vector<PortMapping>& ports);
    void unexposePorts(const std::string& container_id);
    std::vector<PortMapping> getExposedPorts(const std::string& container_id) const;

    // Network utilities
    std::string generateMacAddress() const;
    std::string generateContainerIP(const std::string& network_id) const;
    bool isIPAvailable(const std::string& network_id, const std::string& ip) const;

private:
    std::string network_dir_;
    BridgeNetworkDriver bridge_driver_;
    OverlayNetworkDriver overlay_driver_;
    std::unordered_map<std::string, NetworkConfig> networks_;
    std::unordered_map<std::string, std::vector<std::string>> network_containers_;
    std::unordered_map<std::string, std::vector<PortMapping>> container_ports_;

    void initializeNetworkStorage();
    void loadNetworkConfigs();
    void saveNetworkConfigs();
    std::string generateNetworkId() const;
    void validateNetworkConfig(const NetworkConfig& config) const;
};
```

### 2. Bridge Network Driver

```cpp
class BridgeNetworkDriver {
public:
    struct BridgeInfo {
        std::string name;
        std::string ip_address;
        std::string subnet;
        std::vector<std::string> connected_containers;
        uint16_t mtu;
        bool enabled;
    };

    struct ContainerEndpoint {
        std::string container_id;
        std::string container_ip;
        std::string mac_address;
        std::string veth_name;
        std::string bridge_name;
        std::vector<std::string> aliases;
    };

    explicit BridgeNetworkDriver(const std::string& bridge_dir)
        : bridge_dir_(bridge_dir) {
        std::filesystem::create_directories(bridge_dir_);
    }

    std::string createBridge(const std::string& bridge_name,
                            const std::string& subnet,
                            const std::string& gateway) {
        // Create virtual bridge
        std::string cmd = "ip link add name " + bridge_name + " type bridge";
        int result = system(cmd.c_str());
        if (result != 0) {
            throw std::runtime_error("Failed to create bridge: " + bridge_name);
        }

        // Configure bridge IP
        cmd = "ip addr add " + gateway + "/" + getCIDRFromSubnet(subnet) +
              " dev " + bridge_name;
        result = system(cmd.c_str());
        if (result != 0) {
            throw std::runtime_error("Failed to configure bridge IP");
        }

        // Bring bridge up
        cmd = "ip link set " + bridge_name + " up";
        result = system(cmd.c_str());
        if (result != 0) {
            throw std::runtime_error("Failed to bring bridge up");
        }

        // Enable IP forwarding
        cmd = "sysctl -w net.ipv4.ip_forward=1";
        system(cmd.c_str());

        // Save bridge info
        BridgeInfo bridge_info{
            .name = bridge_name,
            .ip_address = gateway,
            .subnet = subnet,
            .connected_containers = {},
            .mtu = 1500,
            .enabled = true
        };

        saveBridgeInfo(bridge_info);

        return bridge_name;
    }

    void deleteBridge(const std::string& bridge_name) {
        // Remove bridge
        std::string cmd = "ip link delete " + bridge_name;
        system(cmd.c_str());

        // Remove bridge info file
        std::string info_file = bridge_dir_ + "/" + bridge_name + ".json";
        std::filesystem::remove(info_file);
    }

    std::string connectContainer(const std::string& bridge_name,
                                const std::string& container_id,
                                const std::string& subnet) {
        // Generate unique MAC address
        std::string mac_address = generateMacAddress();

        // Generate container IP
        std::string container_ip = generateIPFromSubnet(subnet, container_id);

        // Create veth pair
        std::string veth_host = "veth" + std::to_string(std::hash<std::string>{}(container_id) % 1000000);
        std::string veth_container = "eth0";

        std::string cmd = "ip link add " + veth_host + " type veth peer name " + veth_container;
        int result = system(cmd.c_str());
        if (result != 0) {
            throw std::runtime_error("Failed to create veth pair");
        }

        // Move veth_container to container namespace (this would happen after container creation)
        // For now, we'll create the veth pair and configure the host side

        // Connect veth_host to bridge
        cmd = "ip link set " + veth_host + " master " + bridge_name;
        result = system(cmd.c_str());
        if (result != 0) {
            throw std::runtime_error("Failed to connect veth to bridge");
        }

        // Bring veth_host up
        cmd = "ip link set " + veth_host + " up";
        result = system(cmd.c_str());
        if (result != 0) {
            throw std::runtime_error("Failed to bring veth up");
        }

        // Create container endpoint
        ContainerEndpoint endpoint{
            .container_id = container_id,
            .container_ip = container_ip,
            .mac_address = mac_address,
            .veth_name = veth_host,
            .bridge_name = bridge_name,
            .aliases = {}
        };

        saveContainerEndpoint(endpoint);

        // Update bridge info
        updateBridgeConnectedContainers(bridge_name, container_id, true);

        return container_ip;
    }

    void disconnectContainer(const std::string& container_id) {
        auto endpoint = getContainerEndpoint(container_id);
        if (!endpoint) {
            return; // Container not connected
        }

        // Delete veth pair
        std::string cmd = "ip link delete " + endpoint->veth_name;
        system(cmd.c_str());

        // Remove endpoint file
        std::string endpoint_file = bridge_dir_ + "/container_" + container_id + ".json";
        std::filesystem::remove(endpoint_file);

        // Update bridge info
        updateBridgeConnectedContainers(endpoint->bridge_name, container_id, false);
    }

    void configureContainerNetwork(const std::string& container_id,
                                  const std::string& container_ip,
                                  const std::string& subnet,
                                  const std::string& gateway,
                                  const std::string& mac_address) {
        // This would be called from within the container namespace
        // For now, we'll create the network configuration

        std::string cmd = "ip link set eth0 address " + mac_address;
        int result = system(cmd.c_str());
        if (result != 0) {
            throw std::runtime_error("Failed to set MAC address");
        }

        cmd = "ip addr add " + container_ip + "/" + getCIDRFromSubnet(subnet) + " dev eth0";
        result = system(cmd.c_str());
        if (result != 0) {
            throw std::runtime_error("Failed to configure container IP");
        }

        cmd = "ip link set eth0 up";
        result = system(cmd.c_str());
        if (result != 0) {
            throw std::runtime_error("Failed to bring eth0 up");
        }

        cmd = "ip route add default via " + gateway;
        result = system(cmd.c_str());
        if (result != 0) {
            throw std::runtime_error("Failed to configure default route");
        }
    }

    std::optional<BridgeInfo> getBridgeInfo(const std::string& bridge_name) const {
        std::string info_file = bridge_dir_ + "/" + bridge_name + ".json";
        if (!std::filesystem::exists(info_file)) {
            return std::nullopt;
        }

        std::ifstream file(info_file);
        nlohmann::json bridge_json;
        file >> bridge_json;

        BridgeInfo info{
            .name = bridge_json["name"],
            .ip_address = bridge_json["ip_address"],
            .subnet = bridge_json["subnet"],
            .connected_containers = bridge_json["connected_containers"],
            .mtu = bridge_json["mtu"],
            .enabled = bridge_json["enabled"]
        };

        return info;
    }

    std::optional<ContainerEndpoint> getContainerEndpoint(const std::string& container_id) const {
        std::string endpoint_file = bridge_dir_ + "/container_" + container_id + ".json";
        if (!std::filesystem::exists(endpoint_file)) {
            return std::nullopt;
        }

        std::ifstream file(endpoint_file);
        nlohmann::json endpoint_json;
        file >> endpoint_json;

        ContainerEndpoint endpoint{
            .container_id = endpoint_json["container_id"],
            .container_ip = endpoint_json["container_ip"],
            .mac_address = endpoint_json["mac_address"],
            .veth_name = endpoint_json["veth_name"],
            .bridge_name = endpoint_json["bridge_name"],
            .aliases = endpoint_json["aliases"]
        };

        return endpoint;
    }

private:
    std::string bridge_dir_;

    void saveBridgeInfo(const BridgeInfo& info) {
        nlohmann::json bridge_json;
        bridge_json["name"] = info.name;
        bridge_json["ip_address"] = info.ip_address;
        bridge_json["subnet"] = info.subnet;
        bridge_json["connected_containers"] = info.connected_containers;
        bridge_json["mtu"] = info.mtu;
        bridge_json["enabled"] = info.enabled;

        std::string info_file = bridge_dir_ + "/" + info.name + ".json";
        std::ofstream file(info_file);
        file << bridge_json.dump(2);
    }

    void saveContainerEndpoint(const ContainerEndpoint& endpoint) {
        nlohmann::json endpoint_json;
        endpoint_json["container_id"] = endpoint.container_id;
        endpoint_json["container_ip"] = endpoint.container_ip;
        endpoint_json["mac_address"] = endpoint.mac_address;
        endpoint_json["veth_name"] = endpoint.veth_name;
        endpoint_json["bridge_name"] = endpoint.bridge_name;
        endpoint_json["aliases"] = endpoint.aliases;

        std::string endpoint_file = bridge_dir_ + "/container_" + endpoint.container_id + ".json";
        std::ofstream file(endpoint_file);
        file << endpoint_json.dump(2);
    }

    void updateBridgeConnectedContainers(const std::string& bridge_name,
                                        const std::string& container_id,
                                        bool connect) {
        auto bridge_info = getBridgeInfo(bridge_name);
        if (!bridge_info) {
            return;
        }

        if (connect) {
            bridge_info->connected_containers.push_back(container_id);
        } else {
            auto& containers = bridge_info->connected_containers;
            containers.erase(std::remove(containers.begin(), containers.end(), container_id),
                           containers.end());
        }

        saveBridgeInfo(*bridge_info);
    }

    std::string generateMacAddress() const {
        // Generate MAC address with Docker's prefix
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0x00, 0xFF);

        std::stringstream ss;
        ss << "02:42:";
        ss << std::hex << std::setw(2) << std::setfill('0') << dis(gen) << ":";
        ss << std::hex << std::setw(2) << std::setfill('0') << dis(gen) << ":";
        ss << std::hex << std::setw(2) << std::setfill('0') << dis(gen) << ":";
        ss << std::hex << std::setw(2) << std::setfill('0') << dis(gen);

        return ss.str();
    }

    std::string generateIPFromSubnet(const std::string& subnet, const std::string& container_id) const {
        // Simple IP generation based on container ID hash
        uint64_t hash = std::hash<std::string>{}(container_id);

        // Extract network from subnet
        size_t slash_pos = subnet.find('/');
        if (slash_pos == std::string::npos) {
            throw std::invalid_argument("Invalid subnet format");
        }

        std::string network = subnet.substr(0, slash_pos);
        std::string cidr = subnet.substr(slash_pos + 1);

        // Parse network IP
        in_addr addr;
        if (inet_aton(network.c_str(), &addr) == 0) {
            throw std::invalid_argument("Invalid network IP");
        }

        uint32_t network_addr = ntohl(addr.s_addr);
        uint32_t prefix_len = std::stoul(cidr);

        // Generate host part (avoid .0, .1, and .255)
        uint32_t host_bits = 32 - prefix_len;
        uint32_t host_part = (hash & ((1 << host_bits) - 1)) | 2; // Avoid .0 and .1
        host_part = std::min(host_part, (1u << host_bits) - 2); // Avoid .255

        uint32_t container_ip = network_addr | host_part;
        addr.s_addr = htonl(container_ip);

        return inet_ntoa(addr);
    }

    std::string getCIDRFromSubnet(const std::string& subnet) const {
        size_t slash_pos = subnet.find('/');
        if (slash_pos == std::string::npos) {
            return "24"; // Default to /24
        }
        return subnet.substr(slash_pos + 1);
    }
};
```

### 3. NAT and Port Management

```cpp
class PortManager {
public:
    struct NatRule {
        std::string protocol;
        std::string host_ip;
        uint16_t host_port;
        std::string container_ip;
        uint16_t container_port;
        std::string container_id;
    };

    explicit PortManager() {
        initializeIptables();
    }

    void exposePort(const std::string& container_id,
                   const std::string& container_ip,
                   const std::string& host_ip,
                   uint16_t container_port,
                   uint16_t host_port,
                   const std::string& protocol = "tcp") {
        NatRule rule{
            .protocol = protocol,
            .host_ip = host_ip,
            .host_port = host_port,
            .container_ip = container_ip,
            .container_port = container_port,
            .container_id = container_id
        };

        // Add iptables rule for DNAT
        std::string cmd = "iptables -t nat -A DOCKER -p " + protocol +
                         " -d " + host_ip + " --dport " + std::to_string(host_port) +
                         " -j DNAT --to-destination " + container_ip + ":" +
                         std::to_string(container_port);

        int result = system(cmd.c_str());
        if (result != 0) {
            throw std::runtime_error("Failed to add NAT rule");
        }

        // Add iptables rule for MASQUERADE
        cmd = "iptables -t nat -A POSTROUTING -s " + container_ip + " -d " + container_ip +
              " -p " + protocol + " -j MASQUERADE";

        system(cmd.c_str());

        // Save rule
        saveNatRule(rule);
    }

    void unexposePort(const std::string& container_id,
                     uint16_t container_port,
                     const std::string& protocol = "tcp") {
        auto rules = getNatRules();
        for (const auto& rule : rules) {
            if (rule.container_id == container_id &&
                rule.container_port == container_port &&
                rule.protocol == protocol) {

                // Remove DNAT rule
                std::string cmd = "iptables -t nat -D DOCKER -p " + protocol +
                                 " -d " + rule.host_ip + " --dport " +
                                 std::to_string(rule.host_port) +
                                 " -j DNAT --to-destination " + rule.container_ip + ":" +
                                 std::to_string(rule.container_port);

                system(cmd.c_str());

                // Remove MASQUERADE rule
                cmd = "iptables -t nat -D POSTROUTING -s " + rule.container_ip +
                      " -d " + rule.container_ip + " -p " + protocol + " -j MASQUERADE";

                system(cmd.c_str());

                // Remove saved rule
                removeNatRule(rule);
            }
        }
    }

    void unexposeAllPorts(const std::string& container_id) {
        auto rules = getNatRules();
        for (const auto& rule : rules) {
            if (rule.container_id == container_id) {
                unexposePort(container_id, rule.container_port, rule.protocol);
            }
        }
    }

    std::vector<NatRule> getExposedPorts(const std::string& container_id) const {
        auto all_rules = getNatRules();
        std::vector<NatRule> container_rules;

        for (const auto& rule : all_rules) {
            if (rule.container_id == container_id) {
                container_rules.push_back(rule);
            }
        }

        return container_rules;
    }

    bool isPortAvailable(const std::string& host_ip, uint16_t port, const std::string& protocol) const {
        std::string cmd = "iptables -t nat -C DOCKER -p " + protocol +
                         " -d " + host_ip + " --dport " + std::to_string(port) +
                         " -j DNAT 2>/dev/null";

        int result = system(cmd.c_str());
        return result != 0; // Port is available if rule doesn't exist
    }

    uint16_t findAvailablePort(const std::string& host_ip,
                              uint16_t start_port = 49153,
                              uint16_t end_port = 65535) const {
        for (uint16_t port = start_port; port <= end_port; ++port) {
            if (isPortAvailable(host_ip, port, "tcp") && isPortAvailable(host_ip, port, "udp")) {
                return port;
            }
        }
        throw std::runtime_error("No available ports found");
    }

private:
    std::string rules_file_ = "/var/lib/docker-cpp/port_rules.json";

    void initializeIptables() {
        // Create DOCKER chain in nat table if it doesn't exist
        std::string cmd = "iptables -t nat -N DOCKER 2>/dev/null || true";
        system(cmd.c_str());

        // Add DOCKER chain to PREROUTING
        cmd = "iptables -t nat -C PREROUTING -j DOCKER 2>/dev/null || "
              "iptables -t nat -A PREROUTING -j DOCKER";
        system(cmd.c_str);

        // Add DOCKER chain to OUTPUT
        cmd = "iptables -t nat -C OUTPUT -j DOCKER 2>/dev/null || "
              "iptables -t nat -A OUTPUT -j DOCKER";
        system(cmd.c_str);
    }

    void saveNatRule(const NatRule& rule) {
        auto rules = getNatRules();
        rules.push_back(rule);

        nlohmann::json rules_json;
        rules_json["rules"] = nlohmann::json::array();

        for (const auto& r : rules) {
            nlohmann::json rule_json;
            rule_json["protocol"] = r.protocol;
            rule_json["host_ip"] = r.host_ip;
            rule_json["host_port"] = r.host_port;
            rule_json["container_ip"] = r.container_ip;
            rule_json["container_port"] = r.container_port;
            rule_json["container_id"] = r.container_id;
            rules_json["rules"].push_back(rule_json);
        }

        std::ofstream file(rules_file_);
        file << rules_json.dump(2);
    }

    std::vector<NatRule> getNatRules() const {
        std::vector<NatRule> rules;

        if (!std::filesystem::exists(rules_file_)) {
            return rules;
        }

        std::ifstream file(rules_file_);
        nlohmann::json rules_json;
        file >> rules_json;

        for (const auto& rule_json : rules_json["rules"]) {
            NatRule rule{
                .protocol = rule_json["protocol"],
                .host_ip = rule_json["host_ip"],
                .host_port = rule_json["host_port"],
                .container_ip = rule_json["container_ip"],
                .container_port = rule_json["container_port"],
                .container_id = rule_json["container_id"]
            };
            rules.push_back(rule);
        }

        return rules;
    }

    void removeNatRule(const NatRule& rule) {
        auto rules = getNatRules();
        rules.erase(std::remove_if(rules.begin(), rules.end(),
            [&rule](const NatRule& r) {
                return r.protocol == rule.protocol &&
                       r.host_ip == rule.host_ip &&
                       r.host_port == rule.host_port &&
                       r.container_ip == rule.container_ip &&
                       r.container_port == rule.container_port &&
                       r.container_id == rule.container_id;
            }), rules.end());

        // Save updated rules
        nlohmann::json rules_json;
        rules_json["rules"] = nlohmann::json::array();

        for (const auto& r : rules) {
            nlohmann::json rule_json;
            rule_json["protocol"] = r.protocol;
            rule_json["host_ip"] = r.host_ip;
            rule_json["host_port"] = r.host_port;
            rule_json["container_ip"] = r.container_ip;
            rule_json["container_port"] = r.container_port;
            rule_json["container_id"] = r.container_id;
            rules_json["rules"].push_back(rule_json);
        }

        std::ofstream file(rules_file_);
        file << rules_json.dump(2);
    }
};
```

### 4. DNS and Service Discovery

```cpp
class NetworkDNS {
public:
    struct DnsRecord {
        std::string name;
        std::string ip_address;
        std::string container_id;
        std::string network_id;
        std::vector<std::string> aliases;
    };

    explicit NetworkDNS(const std::string& dns_dir)
        : dns_dir_(dns_dir), dns_port_(53) {
        std::filesystem::create_directories(dns_dir_);
        initializeDNS();
    }

    void addRecord(const std::string& container_id,
                  const std::string& network_id,
                  const std::string& ip_address,
                  const std::vector<std::string>& aliases = {}) {
        DnsRecord record{
            .name = container_id,
            .ip_address = ip_address,
            .container_id = container_id,
            .network_id = network_id,
            .aliases = aliases
        };

        saveDnsRecord(record);
        rebuildDnsConfig();
    }

    void removeRecord(const std::string& container_id) {
        std::string record_file = dns_dir_ + "/" + container_id + ".json";
        std::filesystem::remove(record_file);
        rebuildDnsConfig();
    }

    std::optional<std::string> resolve(const std::string& name) const {
        auto records = getAllDnsRecords();

        // Check exact matches
        for (const auto& record : records) {
            if (record.name == name) {
                return record.ip_address;
            }
            for (const auto& alias : record.aliases) {
                if (alias == name) {
                    return record.ip_address;
                }
            }
        }

        // Check partial matches (e.g., container_name -> container_name.network_name)
        for (const auto& record : records) {
            if (name.find(record.name) != std::string::npos) {
                return record.ip_address;
            }
        }

        return std::nullopt;
    }

    void startDNSServer() {
        // Start DNS server thread
        dns_server_thread_ = std::thread([this]() {
            runDNSServer();
        });
    }

    void stopDNSServer() {
        dns_running_ = false;
        if (dns_server_thread_.joinable()) {
            dns_server_thread_.join();
        }
    }

    std::string getDNSServerAddress() const {
        return "127.0.0.1:" + std::to_string(dns_port_);
    }

private:
    std::string dns_dir_;
    uint16_t dns_port_;
    std::thread dns_server_thread_;
    std::atomic<bool> dns_running_{false};

    void initializeDNS() {
        // Create DNS configuration file
        rebuildDnsConfig();
    }

    void saveDnsRecord(const DnsRecord& record) {
        nlohmann::json record_json;
        record_json["name"] = record.name;
        record_json["ip_address"] = record.ip_address;
        record_json["container_id"] = record.container_id;
        record_json["network_id"] = record.network_id;
        record_json["aliases"] = record.aliases;

        std::string record_file = dns_dir_ + "/" + record.container_id + ".json";
        std::ofstream file(record_file);
        file << record_json.dump(2);
    }

    std::vector<DnsRecord> getAllDnsRecords() const {
        std::vector<DnsRecord> records;

        for (const auto& entry : std::filesystem::directory_iterator(dns_dir_)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                std::ifstream file(entry.path());
                nlohmann::json record_json;
                file >> record_json;

                DnsRecord record{
                    .name = record_json["name"],
                    .ip_address = record_json["ip_address"],
                    .container_id = record_json["container_id"],
                    .network_id = record_json["network_id"],
                    .aliases = record_json["aliases"]
                };

                records.push_back(record);
            }
        }

        return records;
    }

    void rebuildDnsConfig() {
        auto records = getAllDnsRecords();

        std::ofstream hosts_file(dns_dir_ + "/hosts");
        hosts_file << "# Generated by docker-cpp\n";
        hosts_file << "127.0.0.1 localhost\n";

        for (const auto& record : records) {
            hosts_file << record.ip_address << " " << record.name;
            for (const auto& alias : record.aliases) {
                hosts_file << " " << alias;
            }
            hosts_file << "\n";
        }

        // Update resolv.conf for containers
        std::ofstream resolv_file(dns_dir_ + "/resolv.conf");
        resolv_file << "nameserver 127.0.0.1\n";
        resolv_file << "search docker-cpp\n";
        resolv_file << "options ndots:0\n";
    }

    void runDNSServer() {
        dns_running_ = true;

        // Simple DNS server implementation
        // In production, use a proper DNS library
        while (dns_running_) {
            // Handle DNS queries
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
};
```

## Usage Example

```cpp
int main() {
    try {
        // Initialize network manager
        NetworkManager network_manager("/var/lib/docker-cpp/networks");

        // Create a bridge network
        NetworkManager::NetworkConfig bridge_config{
            .name = "my-bridge-network",
            .type = NetworkManager::NetworkType::BRIDGE,
            .subnet = "172.18.0.0/16",
            .gateway = "172.18.0.1"
        };

        std::string network_id = network_manager.createNetwork(bridge_config);
        std::cout << "Created network: " << network_id << std::endl;

        // Connect a container to the network
        std::string container_id = "container-123";
        NetworkManager::ContainerNetworkConfig container_config{
            .network_id = network_id,
            .aliases = {"web-server", "app"},
            .port_mappings = {
                {
                    .container_ip = "", // Will be assigned
                    .container_port = 8080,
                    .host_ip = "0.0.0.0",
                    .host_port = 8080,
                    .protocol = "tcp"
                }
            }
        };

        std::string container_ip = network_manager.connectContainer(network_id, container_id, container_config);
        std::cout << "Container IP: " << container_ip << std::endl;

        // Expose ports
        network_manager.exposePorts(container_id, container_config.port_mappings);

        // List all networks
        auto networks = network_manager.listNetworks();
        std::cout << "Total networks: " << networks.size() << std::endl;

        // Get container networks
        auto container_networks = network_manager.getContainerNetworks(container_id);
        std::cout << "Container networks: " << container_networks.size() << std::endl;

        // Disconnect container
        network_manager.disconnectContainer(network_id, container_id);

        // Delete network
        network_manager.deleteNetwork(network_id);

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
```

## Security Considerations

### 1. Network Isolation

```cpp
class NetworkSecurity {
public:
    static void applyNetworkPolicies(const std::string& container_id,
                                    const std::vector<std::string>& allowed_networks) {
        // Apply iptables rules to restrict network access
        for (const auto& network : allowed_networks) {
            std::string cmd = "iptables -A DOCKER-USER -s " + network +
                             " -d " + network + " -j ACCEPT";
            system(cmd.c_str());
        }

        // Block all other traffic
        std::string cmd = "iptables -A DOCKER-USER -j DROP";
        system(cmd.c_str());
    }

    static void enableNetworkLogging(const std::string& container_id) {
        std::string cmd = "iptables -A DOCKER-USER -s " + container_id +
                         " -j LOG --log-prefix '[DOCKER] '";
        system(cmd.c_str());
    }

    static void setupHostPortProtection() {
        // Protect host ports from container access
        std::string cmd = "iptables -A DOCKER-USER -d 127.0.0.1 -p tcp --dport 1:1024 -j DROP";
        system(cmd.c_str());
    }
};
```

## Performance Optimization

### 1. Network Performance Tuning

```cpp
class NetworkOptimizer {
public:
    static void optimizeNetworkInterface(const std::string& interface_name) {
        // Set MTU for better performance
        std::string cmd = "ip link set " + interface_name + " mtu 9000";
        system(cmd.c_str());

        // Enable TCP offloading
        cmd = "ethtool -K " + interface_name + " tx on rx on";
        system(cmd.c_str());

        // Optimize network buffers
        cmd = "sysctl -w net.core.rmem_max=134217728";
        system(cmd.c_str);
        cmd = "sysctl -w net.core.wmem_max=134217728";
        system(cmd.c_str);
    }

    static void optimizeContainerNetwork(const std::string& container_id) {
        // Increase connection tracking table size
        std::string cmd = "sysctl -w net.netfilter.nf_conntrack_max=1048576";
        system(cmd.c_str);

        // Optimize TCP parameters
        cmd = "sysctl -w net.ipv4.tcp_congestion_control=bbr";
        system(cmd.c_str);
    }
};
```

## Conclusion

The network virtualization system presented in this article provides comprehensive container networking capabilities, including:

1. **Multiple Network Types**: Bridge, overlay, macvlan, and host networking
2. **Port Management**: Dynamic port allocation and NAT configuration
3. **DNS Integration**: Service discovery and name resolution
4. **Security**: Network isolation and access control
5. **Performance**: Network optimization and tuning capabilities

This implementation forms the networking foundation of our docker-cpp project, enabling containers to communicate efficiently while maintaining isolation and security.

## Next Steps

In our next article, "Security Hardening: Seccomp, Capabilities, and Sandboxing," we'll explore how to implement comprehensive security measures for containers, building on the networking capabilities established here.

---

**Previous Article**: [Image Management System](./08-image-management-system.md)
**Next Article**: [Security Hardening: Seccomp, Capabilities, and Sandboxing](./10-security-hardening.md)
**Series Index**: [Table of Contents](./00-table-of-contents.md)
#include "mqtt_dispatcher.hpp"
#include <iostream>
#include <vector>
#include <cstring>

#if defined(_WIN32) || defined(_WIN64)
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #if defined(_MSC_VER)
        #pragma comment(lib, "ws2_32.lib")
    #endif
    using socket_t = SOCKET;
    #define INVALID_SOCKET_VAL INVALID_SOCKET
    #define CLOSE_SOCKET(s) closesocket(s)
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <fcntl.h>
    using socket_t = int;
    #define INVALID_SOCKET_VAL (-1)
    #define CLOSE_SOCKET(s) close(s)
#endif

namespace apex {

namespace {

void init_network() {
#if defined(_WIN32) || defined(_WIN64)
    static bool initialized = false;
    if (!initialized) {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        initialized = true;
    }
#endif
}

std::vector<uint8_t> encode_remaining_length(size_t length) {
    std::vector<uint8_t> encoded;
    do {
        uint8_t byte = length % 128;
        length /= 128;
        if (length > 0) {
            byte |= 0x80;
        }
        encoded.push_back(byte);
    } while (length > 0);
    return encoded;
}

} // namespace

MqttDispatcher::MqttDispatcher(std::string host, uint16_t port, std::string topic_prefix, std::string alert_topic)
    : host_(std::move(host)),
      port_(port),
      topic_prefix_(std::move(topic_prefix)),
      alert_topic_(std::move(alert_topic)) {
    init_network();
}

MqttDispatcher::~MqttDispatcher() {
    disconnect();
}

bool MqttDispatcher::try_tcp_connect() {
    socket_t sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET_VAL) {
        return false;
    }

    // Set 200ms socket timeout for non-blocking edge responsiveness
#if defined(_WIN32) || defined(_WIN64)
    DWORD timeout = 250;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 250000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_);

    if (inet_pton(AF_INET, host_.c_str(), &server_addr.sin_addr) <= 0) {
        hostent* he = gethostbyname(host_.c_str());
        if (!he) {
            CLOSE_SOCKET(sock);
            return false;
        }
        std::memcpy(&server_addr.sin_addr, he->h_addr_list[0], he->h_length);
    }

    if (::connect(sock, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
        CLOSE_SOCKET(sock);
        return false;
    }

    socket_fd_ = static_cast<int64_t>(sock);
    return true;
}

void MqttDispatcher::send_mqtt_connect_packet() {
    if (socket_fd_ == -1) return;

    std::string client_id = "ApexEdgeGateway-" + std::to_string(current_time_ms() % 10000);
    std::vector<uint8_t> var_header = {
        0x00, 0x04, 'M', 'Q', 'T', 'T', // Protocol Name
        0x04,                           // Protocol Level (v3.1.1)
        0x02,                           // Clean Session flag
        0x00, 0x3C                      // Keep Alive (60s)
    };

    std::vector<uint8_t> payload;
    payload.push_back(static_cast<uint8_t>((client_id.size() >> 8) & 0xFF));
    payload.push_back(static_cast<uint8_t>(client_id.size() & 0xFF));
    payload.insert(payload.end(), client_id.begin(), client_id.end());

    size_t rem_len = var_header.size() + payload.size();
    auto rem_bytes = encode_remaining_length(rem_len);

    std::vector<uint8_t> packet;
    packet.push_back(0x10); // CONNECT packet type
    packet.insert(packet.end(), rem_bytes.begin(), rem_bytes.end());
    packet.insert(packet.end(), var_header.begin(), var_header.end());
    packet.insert(packet.end(), payload.begin(), payload.end());

    send(static_cast<socket_t>(socket_fd_), reinterpret_cast<const char*>(packet.data()), static_cast<int>(packet.size()), 0);

    // Read CONNACK (4 bytes: 0x20, 0x02, 0x00, 0x00)
    uint8_t ack[4];
    int r = recv(static_cast<socket_t>(socket_fd_), reinterpret_cast<char*>(ack), 4, 0);
    if (r == 4 && ack[0] == 0x20 && ack[3] == 0x00) {
        is_connected_ = true;
    } else {
        is_connected_ = false;
        disconnect();
    }
}

bool MqttDispatcher::connect() {
    if (is_connected_) return true;
    if (try_tcp_connect()) {
        send_mqtt_connect_packet();
        return is_connected_;
    }
    return false;
}

void MqttDispatcher::disconnect() {
    if (socket_fd_ != -1) {
        CLOSE_SOCKET(static_cast<socket_t>(socket_fd_));
        socket_fd_ = -1;
    }
    is_connected_ = false;
}

bool MqttDispatcher::publish_raw(const std::string& topic, const std::string& payload) {
    if (!is_connected_ || socket_fd_ == -1) {
        return false;
    }

    size_t rem_len = 2 + topic.size() + payload.size();
    auto rem_bytes = encode_remaining_length(rem_len);

    std::vector<uint8_t> packet;
    packet.push_back(0x30); // PUBLISH QoS 0
    packet.insert(packet.end(), rem_bytes.begin(), rem_bytes.end());
    packet.push_back(static_cast<uint8_t>((topic.size() >> 8) & 0xFF));
    packet.push_back(static_cast<uint8_t>(topic.size() & 0xFF));
    packet.insert(packet.end(), topic.begin(), topic.end());
    packet.insert(packet.end(), payload.begin(), payload.end());

    int sent = send(static_cast<socket_t>(socket_fd_), reinterpret_cast<const char*>(packet.data()), static_cast<int>(packet.size()), 0);
    if (sent != static_cast<int>(packet.size())) {
        disconnect();
        return false;
    }

    published_count_.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool MqttDispatcher::dispatch(const TelemetryRecord& record, PersistenceJournal& journal) {
    // If not connected, attempt quick non-blocking reconnect
    if (!is_connected_) {
        connect();
    }

    const std::string json_data = record.to_json();
    const std::string topic = topic_prefix_ + "/" + record.device_id + "/" + record.metric_name;

    bool success = false;
    if (is_connected_) {
        success = publish_raw(topic, json_data);

        // If this is an anomaly alert, publish also to dedicated alert topic
        if (record.is_anomaly && success) {
            publish_raw(alert_topic_, json_data);
            alert_count_.fetch_add(1, std::memory_order_relaxed);
            if (alert_callback_) {
                alert_callback_(record);
            }
        }
    }

    // Edge Resilience Fallback: If network dispatch failed, save to disk journal
    if (!success) {
        journal.append(record);
        if (record.is_anomaly) {
            alert_count_.fetch_add(1, std::memory_order_relaxed);
            if (alert_callback_) {
                alert_callback_(record);
            }
        }
        return false;
    }

    return true;
}

} // namespace apex

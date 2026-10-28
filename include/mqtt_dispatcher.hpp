#pragma once

#include "telemetry_record.hpp"
#include "persistence_journal.hpp"
#include <string>
#include <memory>
#include <atomic>
#include <functional>
#include <cstdint>

namespace apex {

using AlertCallback = std::function<void(const TelemetryRecord&)>;

class MqttDispatcher {
public:
    MqttDispatcher(std::string host, uint16_t port, std::string topic_prefix, std::string alert_topic);
    ~MqttDispatcher();

    bool connect();
    void disconnect();
    [[nodiscard]] bool is_connected() const noexcept { return is_connected_.load(); }

    // Dispatches record. If broker is disconnected, stores in journal.
    bool dispatch(const TelemetryRecord& record, PersistenceJournal& journal);

    void set_alert_callback(AlertCallback cb) { alert_callback_ = std::move(cb); }

    [[nodiscard]] uint64_t published_count() const noexcept { return published_count_.load(); }
    [[nodiscard]] uint64_t alert_count() const noexcept { return alert_count_.load(); }

private:
    bool publish_raw(const std::string& topic, const std::string& payload);
    bool try_tcp_connect();
    void send_mqtt_connect_packet();

    std::string host_;
    uint16_t port_;
    std::string topic_prefix_;
    std::string alert_topic_;
    std::atomic<bool> is_connected_{false};
    std::atomic<uint64_t> published_count_{0};
    std::atomic<uint64_t> alert_count_{0};
    AlertCallback alert_callback_;

    int64_t socket_fd_{-1};
};

} // namespace apex

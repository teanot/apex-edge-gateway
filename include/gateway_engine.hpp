#pragma once

#include "config.hpp"
#include "telemetry_record.hpp"
#include "lock_free_ring_buffer.hpp"
#include "sensor_source.hpp"
#include "anomaly_detector.hpp"
#include "persistence_journal.hpp"
#include "mqtt_dispatcher.hpp"

#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>

namespace apex {

struct GatewayMetrics {
    uint64_t total_ingested{0};
    uint64_t total_processed{0};
    uint64_t total_anomalies{0};
    uint64_t total_dispatched{0};
    uint64_t buffer_dropped{0};
    uint64_t journaled_records{0};
    double current_throughput_ops{0.0};
    size_t ring_buffer_size{0};
    size_t ring_buffer_capacity{0};
};

class GatewayEngine {
public:
    explicit GatewayEngine(GatewayConfig config);
    ~GatewayEngine();

    void start();
    void stop();
    [[nodiscard]] bool is_running() const noexcept { return running_.load(); }

    [[nodiscard]] GatewayMetrics get_metrics() const;

private:
    void poller_loop();
    void processor_loop();
    void journal_replay_loop();

    GatewayConfig config_;
    SPSCRingBuffer<TelemetryRecord> ring_buffer_;
    std::vector<std::unique_ptr<SimulatedDeviceNode>> sensor_nodes_;
    AnomalyDetector anomaly_detector_;
    PersistenceJournal journal_;
    MqttDispatcher dispatcher_;

    std::atomic<bool> running_{false};
    std::thread poller_thread_;
    std::thread processor_thread_;
    std::thread replay_thread_;

    // Performance counters
    std::atomic<uint64_t> total_ingested_{0};
    std::atomic<uint64_t> total_processed_{0};
    std::atomic<uint64_t> total_anomalies_{0};
    std::atomic<uint64_t> total_dispatched_{0};

    std::chrono::steady_clock::time_point start_time_;
    mutable std::chrono::steady_clock::time_point last_metric_time_;
    mutable uint64_t last_metric_count_{0};
    mutable double current_throughput_{0.0};
};

} // namespace apex

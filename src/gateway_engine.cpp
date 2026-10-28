#include "gateway_engine.hpp"
#include <iostream>
#include <thread>

namespace apex {

GatewayEngine::GatewayEngine(GatewayConfig config)
    : config_(std::move(config)),
      ring_buffer_(config_.ring_buffer_capacity),
      sensor_nodes_(create_default_industrial_cluster(config_.simulated_devices_count)),
      anomaly_detector_(config_.anomaly_z_threshold, config_.anomaly_warmup_samples, config_.anomaly_ema_alpha),
      journal_(config_.journal_file_path),
      dispatcher_(config_.mqtt_broker_host, config_.mqtt_broker_port, config_.mqtt_topic_prefix, config_.mqtt_alert_topic) {
    
    start_time_ = std::chrono::steady_clock::now();
    last_metric_time_ = start_time_;
}

GatewayEngine::~GatewayEngine() {
    stop();
}

void GatewayEngine::start() {
    if (running_.exchange(true)) {
        return;
    }

    dispatcher_.connect();

    poller_thread_ = std::thread(&GatewayEngine::poller_loop, this);
    processor_thread_ = std::thread(&GatewayEngine::processor_loop, this);
    replay_thread_ = std::thread(&GatewayEngine::journal_replay_loop, this);
}

void GatewayEngine::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    if (poller_thread_.joinable()) poller_thread_.join();
    if (processor_thread_.joinable()) processor_thread_.join();
    if (replay_thread_.joinable()) replay_thread_.join();

    dispatcher_.disconnect();
}

void GatewayEngine::poller_loop() {
    while (running_) {
        for (auto& node : sensor_nodes_) {
            node->poll(ring_buffer_);
            total_ingested_.fetch_add(4, std::memory_order_relaxed); // 4 channels per node
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.sensor_poll_interval_ms));
    }
}

void GatewayEngine::processor_loop() {
    TelemetryRecord record;
    while (running_) {
        bool had_data = false;
        while (ring_buffer_.pop(record)) {
            had_data = true;
            total_processed_.fetch_add(1, std::memory_order_relaxed);

            bool is_anomaly = anomaly_detector_.process(record);
            if (is_anomaly) {
                total_anomalies_.fetch_add(1, std::memory_order_relaxed);
            }

            if (dispatcher_.dispatch(record, journal_)) {
                total_dispatched_.fetch_add(1, std::memory_order_relaxed);
            }
        }

        if (!had_data) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
}

void GatewayEngine::journal_replay_loop() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(2));

        if (dispatcher_.is_connected() && journal_.pending_records_count() > 0) {
            auto batch = journal_.read_batch(50);
            if (!batch.empty()) {
                size_t replayed = 0;
                for (const auto& rec : batch) {
                    if (dispatcher_.dispatch(rec, journal_)) {
                        total_dispatched_.fetch_add(1, std::memory_order_relaxed);
                        ++replayed;
                    } else {
                        break; // Connection lost again
                    }
                }
                if (replayed > 0) {
                    journal_.commit_replayed(replayed);
                }
            }
        }
    }
}

GatewayMetrics GatewayEngine::get_metrics() const {
    GatewayMetrics m;
    m.total_ingested = total_ingested_.load(std::memory_order_relaxed);
    m.total_processed = total_processed_.load(std::memory_order_relaxed);
    m.total_anomalies = total_anomalies_.load(std::memory_order_relaxed);
    m.total_dispatched = total_dispatched_.load(std::memory_order_relaxed);
    m.buffer_dropped = ring_buffer_.dropped_count();
    m.journaled_records = journal_.pending_records_count();
    m.ring_buffer_size = ring_buffer_.size();
    m.ring_buffer_capacity = ring_buffer_.capacity();

    auto now = std::chrono::steady_clock::now();
    double elapsed_sec = std::chrono::duration<double>(now - last_metric_time_).count();
    if (elapsed_sec >= 0.5) {
        uint64_t diff = m.total_processed - last_metric_count_;
        current_throughput_ = static_cast<double>(diff) / elapsed_sec;
        last_metric_count_ = m.total_processed;
        last_metric_time_ = now;
    }
    m.current_throughput_ops = current_throughput_;

    return m;
}

} // namespace apex

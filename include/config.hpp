#pragma once
#include <string>
#include <cstdint>

namespace apex {

struct GatewayConfig {
    std::string gateway_id = "apex-edge-node-01";
    std::string mqtt_broker_host = "127.0.0.1";
    uint16_t mqtt_broker_port = 1883;
    std::string mqtt_topic_prefix = "apex/telemetry";
    std::string mqtt_alert_topic = "apex/alerts";
    
    // Sampling & processing configuration
    uint32_t sensor_poll_interval_ms = 100; // 10 Hz per sensor
    size_t ring_buffer_capacity = 4096;
    
    // Anomaly detection
    double anomaly_z_threshold = 3.0; // standard deviations
    size_t anomaly_warmup_samples = 30;
    double anomaly_ema_alpha = 0.1; // exponential moving average weight
    
    // Resilience & persistence
    std::string journal_file_path = "apex_journal.bin";
    size_t journal_max_records = 50000;
    bool enable_offline_journaling = true;
    
    // Simulated sensors count
    size_t simulated_devices_count = 3;
};

} // namespace apex

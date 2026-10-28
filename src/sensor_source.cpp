#include "sensor_source.hpp"
#include <algorithm>
#include <cmath>

namespace apex {

SimulatedDeviceNode::SimulatedDeviceNode(std::string device_id, std::vector<SimulatedSensorChannel> channels)
    : device_id_(std::move(device_id)),
      channels_(std::move(channels)),
      rng_(std::random_device{}()) {}

void SimulatedDeviceNode::poll(SPSCRingBuffer<TelemetryRecord>& queue) {
    const uint64_t now = current_time_ms();
    ++step_count_;

    for (const auto& ch : channels_) {
        // Slow sinusoidal drift to simulate daily industrial duty cycle
        const double drift = std::sin(static_cast<double>(step_count_) * 0.05) * (ch.noise_std * 0.5);
        const double noise = normal_dist_(rng_) * ch.noise_std;
        double value = ch.base_value + drift + noise;

        // Periodic simulated anomalies (e.g., sudden thermal spike or mechanical vibration)
        if (uniform_dist_(rng_) < ch.anomaly_probability) {
            const double spike_direction = (uniform_dist_(rng_) > 0.5) ? 1.0 : -1.0;
            value += spike_direction * ch.noise_std * ch.anomaly_spike_factor;
        }

        // Clamp to physical sensor limits
        value = std::clamp(value, ch.min_val, ch.max_val);

        TelemetryRecord rec(now, device_id_, ch.metric_name, value, ch.unit);
        queue.push(rec);
    }
}

std::vector<std::unique_ptr<SimulatedDeviceNode>> create_default_industrial_cluster(size_t node_count) {
    std::vector<std::unique_ptr<SimulatedDeviceNode>> cluster;
    cluster.reserve(node_count);

    for (size_t i = 1; i <= node_count; ++i) {
        char dev_buf[32];
        std::snprintf(dev_buf, sizeof(dev_buf), "industrial-unit-%02zu", i);

        std::vector<SimulatedSensorChannel> channels = {
            {
                .metric_name = "vibration_rms",
                .unit = "mm/s",
                .base_value = 2.4 + (i * 0.1),
                .noise_std = 0.35,
                .min_val = 0.0,
                .max_val = 25.0,
                .anomaly_probability = 0.02, // 2% chance of anomaly
                .anomaly_spike_factor = 5.5
            },
            {
                .metric_name = "bearing_temp",
                .unit = "degC",
                .base_value = 65.0 + (i * 1.5),
                .noise_std = 1.2,
                .min_val = 15.0,
                .max_val = 130.0,
                .anomaly_probability = 0.015,
                .anomaly_spike_factor = 6.0
            },
            {
                .metric_name = "bus_voltage",
                .unit = "V",
                .base_value = 400.0,
                .noise_std = 2.5,
                .min_val = 340.0,
                .max_val = 460.0,
                .anomaly_probability = 0.01,
                .anomaly_spike_factor = 4.5
            },
            {
                .metric_name = "active_power",
                .unit = "kW",
                .base_value = 45.0 + (i * 5.0),
                .noise_std = 1.8,
                .min_val = 0.0,
                .max_val = 120.0,
                .anomaly_probability = 0.02,
                .anomaly_spike_factor = 5.0
            }
        };

        cluster.push_back(std::make_unique<SimulatedDeviceNode>(dev_buf, std::move(channels)));
    }

    return cluster;
}

} // namespace apex

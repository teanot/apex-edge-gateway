#pragma once

#include "telemetry_record.hpp"
#include "lock_free_ring_buffer.hpp"
#include <string>
#include <vector>
#include <memory>
#include <random>

namespace apex {

class ISensorSource {
public:
    virtual ~ISensorSource() = default;
    virtual void poll(SPSCRingBuffer<TelemetryRecord>& queue) = 0;
};

struct SimulatedSensorChannel {
    std::string metric_name;
    std::string unit;
    double base_value;
    double noise_std;
    double min_val;
    double max_val;
    double anomaly_probability;
    double anomaly_spike_factor;
};

class SimulatedDeviceNode : public ISensorSource {
public:
    SimulatedDeviceNode(std::string device_id, std::vector<SimulatedSensorChannel> channels);

    void poll(SPSCRingBuffer<TelemetryRecord>& queue) override;

    [[nodiscard]] const std::string& device_id() const noexcept { return device_id_; }

private:
    std::string device_id_;
    std::vector<SimulatedSensorChannel> channels_;
    std::mt19937 rng_;
    std::normal_distribution<double> normal_dist_{0.0, 1.0};
    std::uniform_real_distribution<double> uniform_dist_{0.0, 1.0};
    uint64_t step_count_{0};
};

std::vector<std::unique_ptr<SimulatedDeviceNode>> create_default_industrial_cluster(size_t node_count = 3);

} // namespace apex

#pragma once

#include "telemetry_record.hpp"
#include <string>
#include <unordered_map>
#include <cmath>
#include <mutex>

namespace apex {

struct StreamStatistics {
    uint64_t count{0};
    double mean{0.0};
    double M2{0.0};       // Sum of squared differences for Welford's algorithm
    double ema_mean{0.0};  // Exponential moving average
    double ema_var{0.0};   // Exponential moving variance

    void update(double val, double alpha = 0.1) {
        ++count;
        // Welford's algorithm for numerically stable variance
        double delta = val - mean;
        mean += delta / static_cast<double>(count);
        double delta2 = val - mean;
        M2 += delta * delta2;

        // Exponential Moving Average
        if (count == 1) {
            ema_mean = val;
            ema_var = 1.0;
        } else {
            double diff = val - ema_mean;
            ema_mean += alpha * diff;
            ema_var = (1.0 - alpha) * (ema_var + alpha * diff * diff);
        }
    }

    [[nodiscard]] double variance() const noexcept {
        return (count > 1) ? (M2 / static_cast<double>(count - 1)) : 1.0;
    }

    [[nodiscard]] double std_dev() const noexcept {
        double v = variance();
        return (v > 1e-9) ? std::sqrt(v) : 1e-4;
    }
};

class AnomalyDetector {
public:
    AnomalyDetector(double z_threshold = 3.0, size_t warmup_samples = 30, double ema_alpha = 0.1);

    // Analyzes the record in-place: updates stats, sets is_anomaly and anomaly_score
    bool process(TelemetryRecord& record);

    [[nodiscard]] size_t tracked_streams_count() const;
    void reset();

private:
    double z_threshold_;
    size_t warmup_samples_;
    double ema_alpha_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, StreamStatistics> streams_;
};

} // namespace apex

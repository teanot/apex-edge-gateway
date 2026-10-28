#include "anomaly_detector.hpp"
#include <cmath>

namespace apex {

AnomalyDetector::AnomalyDetector(double z_threshold, size_t warmup_samples, double ema_alpha)
    : z_threshold_(z_threshold),
      warmup_samples_(warmup_samples),
      ema_alpha_(ema_alpha) {}

bool AnomalyDetector::process(TelemetryRecord& record) {
    const std::string key = std::string(record.device_id) + "::" + record.metric_name;

    std::lock_guard<std::mutex> lock(mutex_);
    auto& stats = streams_[key];

    stats.update(record.value, ema_alpha_);

    if (stats.count < warmup_samples_) {
        record.is_anomaly = 0;
        record.anomaly_score = 0.0;
        return false;
    }

    const double sigma = stats.std_dev();
    const double z_score = (sigma > 1e-6) ? std::abs(record.value - stats.ema_mean) / sigma : 0.0;
    record.anomaly_score = z_score;

    if (z_score >= z_threshold_) {
        record.is_anomaly = 1;
        return true;
    }

    record.is_anomaly = 0;
    return false;
}

size_t AnomalyDetector::tracked_streams_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return streams_.size();
}

void AnomalyDetector::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    streams_.clear();
}

} // namespace apex

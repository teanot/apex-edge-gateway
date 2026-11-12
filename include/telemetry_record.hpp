#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <concepts>
#include <array>

namespace apex {

#pragma pack(push, 1)
struct TelemetryRecord {
    uint64_t timestamp_ms{0};
    char device_id[32]{0};
    char metric_name[32]{0};
    double value{0.0};
    char unit[16]{0};
    uint8_t is_anomaly{0}; // 0 = normal, 1 = anomaly
    double anomaly_score{0.0}; // Z-score or deviation magnitude

    TelemetryRecord() = default;

        TelemetryRecord(uint64_t ts, std::string_view dev, std::string_view metric, double val, std::string_view u)
        : timestamp_ms(ts), value(val), is_anomaly(0), anomaly_score(0.0) {
        set_device_id(dev);
        set_metric_name(metric);
        set_unit(u);
    }

    void set_device_id(std::string_view dev) noexcept {
        const size_t len = std::min(dev.size(), sizeof(device_id) - 1);
        std::copy_n(dev.data(), len, device_id);
        device_id[len] = '\0';
    }

    void set_metric_name(std::string_view metric) noexcept {
        const size_t len = std::min(metric.size(), sizeof(metric_name) - 1);
        std::copy_n(metric.data(), len, metric_name);
        metric_name[len] = '\0';
    }

    void set_unit(std::string_view u) noexcept {
        const size_t len = std::min(u.size(), sizeof(unit) - 1);
        std::copy_n(u.data(), len, unit);
        unit[len] = '\0';
    }

    [[nodiscard]] std::string to_json() const {
        std::ostringstream oss;
        oss << "{"
            << "\"timestamp_ms\":" << timestamp_ms << ","
            << "\"device_id\":\"" << device_id << "\","
            << "\"metric_name\":\"" << metric_name << "\","
            << "\"value\":" << std::fixed << std::setprecision(3) << value << ","
            << "\"unit\":\"" << unit << "\","
            << "\"is_anomaly\":" << (is_anomaly ? "true" : "false") << ","
            << "\"anomaly_score\":" << std::fixed << std::setprecision(2) << anomaly_score
            << "}";
        return oss.str();
    }
};
#pragma pack(pop)

static_assert(sizeof(TelemetryRecord) <= 128, "TelemetryRecord must be compact and cache-friendly");

// C++20 Concept
template <typename T>
concept TelemetryData = requires(T t) {
    { t.timestamp_ms } -> std::convertible_to<uint64_t>;
    { t.value } -> std::convertible_to<double>;
    { t.to_json() } -> std::same_as<std::string>;
};

static_assert(TelemetryData<TelemetryRecord>, "TelemetryRecord satisfies TelemetryData concept");

inline uint64_t current_time_ms() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

} // namespace apex


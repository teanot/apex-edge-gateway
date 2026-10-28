#include "test_harness.hpp"
#include "anomaly_detector.hpp"

TEST_CASE(AnomalyDetector_WarmupPeriod) {
    apex::AnomalyDetector detector(3.0, 10, 0.1);
    apex::TelemetryRecord rec(1000, "dev-01", "vibration", 5.0, "mm/s");

    // First 9 samples should not trigger anomalies due to warmup
    for (int i = 0; i < 9; ++i) {
        rec.value = 5.0 + (i % 2);
        bool is_anom = detector.process(rec);
        REQUIRE_FALSE(is_anom);
        REQUIRE(rec.is_anomaly == 0);
    }
}

TEST_CASE(AnomalyDetector_DetectsSuddenSpike) {
    apex::AnomalyDetector detector(3.0, 20, 0.1);
    apex::TelemetryRecord rec(1000, "pump-01", "temp", 50.0, "degC");

    // Feed 30 steady state normal samples (mean ~50, small variance)
    for (int i = 0; i < 30; ++i) {
        rec.timestamp_ms += 100;
        rec.value = 50.0 + (i % 3) * 0.1;
        detector.process(rec);
    }

    // Now inject severe thermal spike
    rec.timestamp_ms += 100;
    rec.value = 95.0; // Huge spike compared to 50.0
    bool is_anom = detector.process(rec);

    REQUIRE(is_anom);
    REQUIRE(rec.is_anomaly == 1);
    REQUIRE(rec.anomaly_score > 3.0);
}

TEST_CASE(AnomalyDetector_MultipleStreamsIsolation) {
    apex::AnomalyDetector detector(3.0, 15, 0.1);

    // Warm up stream A
    for (int i = 0; i < 20; ++i) {
        apex::TelemetryRecord rA(1000 + i, "dev-A", "pressure", 10.0 + (i % 2) * 0.1, "bar");
        detector.process(rA);
    }

    // Stream B has higher normal baseline
    for (int i = 0; i < 20; ++i) {
        apex::TelemetryRecord rB(1000 + i, "dev-B", "pressure", 100.0 + (i % 2) * 0.1, "bar");
        detector.process(rB);
    }

    REQUIRE(detector.tracked_streams_count() == 2);

    // Value 15.0 on stream A should be anomalous (baseline 10.0)
    apex::TelemetryRecord testA(2000, "dev-A", "pressure", 15.0, "bar");
    REQUIRE(detector.process(testA));

    // Value 100.1 on stream B is normal (baseline 100.0)
    apex::TelemetryRecord testB(2000, "dev-B", "pressure", 100.1, "bar");
    REQUIRE_FALSE(detector.process(testB));
}

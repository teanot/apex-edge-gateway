"""Unit, regression, and end-to-end tests for the ApexEdge telemetry dashboard."""

from pathlib import Path
import struct
import sys
import time

from fastapi.testclient import TestClient
import pytest

DASHBOARD_DIR = Path(__file__).resolve().parent.parent / "dashboard"
if str(DASHBOARD_DIR) not in sys.path:
    sys.path.insert(0, str(DASHBOARD_DIR))

import app as dashboard_app  # noqa: E402


def _make_payload(
    device_id: str = "sensor-1",
    metric_name: str = "temp",
    value: float = 24.5,
    unit: str = "C",
    is_anomaly: bool = False,
    anomaly_score: float = 0.0,
    timestamp_ms: int = 1_700_000_000_000,
) -> dict:
    """Build a deterministic telemetry payload matching dashboard expectations."""
    return {
        "timestamp_ms": timestamp_ms,
        "device_id": device_id,
        "metric_name": metric_name,
        "value": value,
        "unit": unit,
        "is_anomaly": is_anomaly,
        "anomaly_score": anomaly_score,
    }


@pytest.fixture(autouse=True)
def reset_dashboard_state(monkeypatch: pytest.MonkeyPatch):
    """Isolate shared in-memory state and suppress background threads per test."""
    monkeypatch.setattr(dashboard_app, "background_telemetry_listener", lambda: None)
    dashboard_app.telemetry_history.clear()
    dashboard_app.recent_alerts.clear()
    dashboard_app.active_connections.clear()
    dashboard_app.stats.update(
        {
            "total_received": 0,
            "total_anomalies": 0,
            "active_devices": set(),
            "start_time": time.time(),
        }
    )


# --- Unit Tests ---


def test_record_telemetry_updates_stats_and_history():
    payload = _make_payload(device_id="sensor-1", metric_name="temp", value=25.0)
    dashboard_app.record_telemetry(payload)

    assert dashboard_app.stats["total_received"] == 1
    assert dashboard_app.stats["total_anomalies"] == 0
    assert dashboard_app.stats["active_devices"] == {"sensor-1"}
    assert list(dashboard_app.telemetry_history["sensor-1::temp"]) == [payload]
    assert len(dashboard_app.recent_alerts) == 0


def test_record_telemetry_tracks_anomalies_and_caps_alerts():
    for i in range(25):
        dashboard_app.record_telemetry(
            _make_payload(
                device_id=f"dev-{i}",
                metric_name="voltage",
                value=250.0 + i,
                is_anomaly=True,
                anomaly_score=3.5 + i * 0.1,
                timestamp_ms=1_700_000_000_000 + i,
            )
        )

    assert dashboard_app.stats["total_received"] == 25
    assert dashboard_app.stats["total_anomalies"] == 25
    assert len(dashboard_app.recent_alerts) == 20
    assert dashboard_app.recent_alerts[0]["device_id"] == "dev-24"
    assert dashboard_app.recent_alerts[-1]["device_id"] == "dev-5"


def test_record_telemetry_respects_history_limit():
    total = dashboard_app.HISTORY_LIMIT + 10
    for i in range(total):
        dashboard_app.record_telemetry(
            _make_payload(device_id="edge-node", metric_name="load", value=float(i))
        )

    history = dashboard_app.telemetry_history["edge-node::load"]
    assert len(history) == dashboard_app.HISTORY_LIMIT
    assert history[0]["value"] == 10.0
    assert history[-1]["value"] == float(total - 1)


# --- Regression Tests ---


def test_stats_endpoint_zero_division_guard():
    with TestClient(dashboard_app.app) as client:
        response = client.get("/api/stats")

    assert response.status_code == 200
    body = response.json()
    assert body["total_received"] == 0
    assert body["total_anomalies"] == 0
    assert body["anomaly_rate_pct"] == 0.0
    assert body["active_devices_count"] == 0
    assert body["recent_alerts"] == []


def test_record_telemetry_missing_optional_fields():
    dashboard_app.record_telemetry({})

    assert dashboard_app.stats["total_received"] == 1
    assert dashboard_app.stats["total_anomalies"] == 0
    assert dashboard_app.stats["active_devices"] == {"unknown"}
    assert len(dashboard_app.telemetry_history["unknown::unknown"]) == 1


def test_binary_journal_record_struct_unpack_regression():
    """Verify binary journal decoding matches the 112-byte C++ TelemetryRecord layout."""
    assert dashboard_app.JOURNAL_RECORD_SIZE == 112

    packed = struct.pack(
        "<Qdd32s32s16sB7x",
        1_700_000_123_456,
        412.758,
        4.25,
        b"turbine-01".ljust(32, b"\x00"),
        b"wing_strain".ljust(32, b"\x00"),
        b"uStrain".ljust(16, b"\x00"),
        1,
    )
    payload = dashboard_app.parse_journal_record(packed)

    assert payload == {
        "timestamp_ms": 1_700_000_123_456,
        "device_id": "turbine-01",
        "metric_name": "wing_strain",
        "value": 412.758,
        "unit": "uStrain",
        "is_anomaly": True,
        "anomaly_score": 4.25,
    }

    with pytest.raises(ValueError, match="112-byte"):
        dashboard_app.parse_journal_record(packed[:-1])


# --- End-to-End API & WebSocket Tests ---


def test_e2e_index_returns_html():
    with TestClient(dashboard_app.app) as client:
        response = client.get("/")

    assert response.status_code == 200
    assert "text/html" in response.headers["content-type"]
    assert "ApexEdge" in response.text


def test_e2e_ingest_and_stats_api():
    dashboard_app.record_telemetry(_make_payload(device_id="node-a", metric_name="temp", value=21.0))
    dashboard_app.record_telemetry(_make_payload(device_id="node-a", metric_name="volt", value=230.0))
    dashboard_app.record_telemetry(_make_payload(device_id="node-b", metric_name="temp", value=22.5))
    dashboard_app.record_telemetry(
        _make_payload(
            device_id="node-b",
            metric_name="temp",
            value=99.9,
            is_anomaly=True,
            anomaly_score=5.4,
        )
    )

    with TestClient(dashboard_app.app) as client:
        response = client.get("/api/stats")

    assert response.status_code == 200
    body = response.json()
    assert body["total_received"] == 4
    assert body["total_anomalies"] == 1
    assert body["anomaly_rate_pct"] == 25.0
    assert body["active_devices_count"] == 2
    assert len(body["recent_alerts"]) == 1
    assert body["recent_alerts"][0]["device_id"] == "node-b"


def test_e2e_websocket_delivers_snapshot():
    normal = _make_payload(device_id="node-ws", metric_name="pressure", value=101.3)
    anomaly = _make_payload(
        device_id="node-ws",
        metric_name="pressure",
        value=180.0,
        is_anomaly=True,
        anomaly_score=4.8,
    )
    dashboard_app.record_telemetry(normal)
    dashboard_app.record_telemetry(anomaly)

    with TestClient(dashboard_app.app) as client:
        with client.websocket_connect("/ws") as websocket:
            snapshot = websocket.receive_json()

    assert snapshot["type"] == "snapshot"
    assert "node-ws::pressure" in snapshot["data"]
    assert len(snapshot["data"]["node-ws::pressure"]) == 2
    assert len(snapshot["alerts"]) == 1
    assert snapshot["alerts"][0]["value"] == 180.0

import asyncio
import json
import os
import sys
import threading
import time
from collections import deque
from pathlib import Path
from typing import Dict, List

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.responses import HTMLResponse
from fastapi.templating import Jinja2Templates
from starlette.requests import Request

# Optional paho-mqtt support
try:
    import paho.mqtt.client as mqtt
    HAVE_MQTT = True
except ImportError:
    HAVE_MQTT = False

app = FastAPI(title="ApexEdge Telemetry & Anomaly Analytics", version="1.0.0")

BASE_DIR = Path(__file__).resolve().parent
templates = Jinja2Templates(directory=str(BASE_DIR / "templates"))

# In-memory rolling telemetry cache
HISTORY_LIMIT = 50
telemetry_history: Dict[str, deque] = {}
recent_alerts: deque = deque(maxlen=20)
active_connections: List[WebSocket] = []

stats = {
    "total_received": 0,
    "total_anomalies": 0,
    "active_devices": set(),
    "start_time": time.time(),
}

def record_telemetry(payload: dict):
    stats["total_received"] += 1
    device_id = payload.get("device_id", "unknown")
    metric = payload.get("metric_name", "unknown")
    stats["active_devices"].add(device_id)

    key = f"{device_id}::{metric}"
    if key not in telemetry_history:
        telemetry_history[key] = deque(maxlen=HISTORY_LIMIT)
    telemetry_history[key].append(payload)

    if payload.get("is_anomaly", False):
        stats["total_anomalies"] += 1
        recent_alerts.appendleft(payload)

async def broadcast_event(event_type: str, data: dict):
    message = json.dumps({"type": event_type, "data": data})
    disconnected = []
    for ws in active_connections:
        try:
            await ws.send_text(message)
        except Exception:
            disconnected.append(ws)
    for ws in disconnected:
        if ws in active_connections:
            active_connections.remove(ws)

# Background simulated generator if MQTT broker is offline
def background_telemetry_listener():
    mqtt_host = os.environ.get("MQTT_HOST", "127.0.0.1")
    mqtt_port = int(os.environ.get("MQTT_PORT", "1883"))
    connected = False

    if HAVE_MQTT:
        def on_connect(client, userdata, flags, rc, properties=None):
            if rc == 0:
                print(f"[Dashboard] Connected to MQTT broker at {mqtt_host}:{mqtt_port}")
                client.subscribe("apex/telemetry/#")
                client.subscribe("apex/alerts")

        def on_message(client, userdata, msg):
            try:
                payload = json.loads(msg.payload.decode("utf-8"))
                record_telemetry(payload)
                # Schedule async broadcast via event loop
                if loop and loop.is_running():
                    asyncio.run_coroutine_threadsafe(
                        broadcast_event("telemetry", payload), loop
                    )
            except Exception as e:
                pass

        try:
            client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="ApexDashboardClient")
            client.on_connect = on_connect
            client.on_message = on_message
            client.connect(mqtt_host, mqtt_port, keepalive=60)
            client.loop_start()
            connected = True
        except Exception as e:
            print(f"[Dashboard] MQTT connection to {mqtt_host}:{mqtt_port} failed ({e}). Monitoring journal file.")

    # Fallback: check apex_journal.bin or generate realistic telemetry stream
    journal_path = Path(BASE_DIR.parent / "build" / "apex_journal.bin")
    if not journal_path.exists():
        journal_path = Path(BASE_DIR.parent / "apex_journal.bin")

    last_pos = 0
    while True:
        time.sleep(0.5)
        if journal_path.exists():
            try:
                current_size = journal_path.stat().st_size
                if current_size > last_pos:
                    with open(journal_path, "rb") as f:
                        f.seek(last_pos)
                        # Read 97-byte structs
                        while chunk := f.read(97):
                            if len(chunk) < 97:
                                break
                            # Parse raw struct
                            import struct
                            ts, dev_raw, metric_raw, val, unit_raw, is_anom, anom_score = struct.unpack("<Q32s32sd16sBd", chunk)
                            dev = dev_raw.split(b'\x00')[0].decode('ascii', errors='ignore')
                            metric = metric_raw.split(b'\x00')[0].decode('ascii', errors='ignore')
                            unit = unit_raw.split(b'\x00')[0].decode('ascii', errors='ignore')
                            
                            payload = {
                                "timestamp_ms": ts,
                                "device_id": dev,
                                "metric_name": metric,
                                "value": round(val, 3),
                                "unit": unit,
                                "is_anomaly": bool(is_anom),
                                "anomaly_score": round(anom_score, 2)
                            }
                            record_telemetry(payload)
                            if loop and loop.is_running():
                                asyncio.run_coroutine_threadsafe(
                                    broadcast_event("telemetry", payload), loop
                                )
                    last_pos = current_size
            except Exception:
                pass

loop = None

@app.on_event("startup")
def startup_event():
    global loop
    loop = asyncio.get_event_loop()
    t = threading.Thread(target=background_telemetry_listener, daemon=True)
    t.start()

@app.get("/", response_class=HTMLResponse)
async def get_index(request: Request):
    return templates.TemplateResponse("index.html", {"request": request})

@app.get("/api/stats")
async def get_stats():
    uptime = time.time() - stats["start_time"]
    rate = stats["total_received"] / uptime if uptime > 0 else 0
    return {
        "uptime_sec": int(uptime),
        "total_received": stats["total_received"],
        "total_anomalies": stats["total_anomalies"],
        "anomaly_rate_pct": round((stats["total_anomalies"] / max(1, stats["total_received"])) * 100, 2),
        "active_devices_count": len(stats["active_devices"]),
        "ingestion_rate_ops": round(rate, 1),
        "recent_alerts": list(recent_alerts)[:10],
    }

@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await websocket.accept()
    active_connections.append(websocket)
    try:
        # Send initial snapshot of telemetry history
        snapshot = {
            "type": "snapshot",
            "data": {k: list(v) for k, v in telemetry_history.items()},
            "alerts": list(recent_alerts),
        }
        await websocket.send_text(json.dumps(snapshot))
        while True:
            # Keep socket alive
            await websocket.receive_text()
    except WebSocketDisconnect:
        if websocket in active_connections:
            active_connections.remove(websocket)

if __name__ == "__main__":
    import uvicorn
    uvicorn.run("app:app", host="0.0.0.0", port=8000, reload=False)

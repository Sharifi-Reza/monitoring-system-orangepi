from fastapi import FastAPI
from pydantic import BaseModel
import requests
import urllib3

# Suppress insecure HTTPS warnings because we are using a self-signed cert in C
urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

app = FastAPI(
    title="Smart Security System API - 401101932",
    description="Thin Swagger documentation gateway forwarding requests to the C backend.",
    version="1.0.0"
)

# This points directly to your C server running locally on the Orange Pi
C_BACKEND_BASE = "https://127.0.0.1:8443"

class CommandRequest(BaseModel):
    cmd: str

class GuardRequest(BaseModel):
    state: int

@app.get("/api/v1/stream", tags=["Stream"])
def get_stream_url():
    """Returns the redirect URL for the MJPEG video stream."""
    res = requests.get(f"{C_BACKEND_BASE}/api/v1/stream", verify=False, allow_redirects=False)
    return {"stream_url": res.headers.get("Location", "Stream not found")}

@app.get("/api/v1/telemetry", tags=["Telemetry"])
def get_telemetry():
    """Gets CPU temperature, Memory, CPU Load, and Thermal Status from the C backend."""
    res = requests.get(f"{C_BACKEND_BASE}/api/v1/telemetry", verify=False)
    return res.json()

@app.get("/api/v1/persons", tags=["Detection"])
def get_persons():
    """Gets the current person count."""
    res = requests.get(f"{C_BACKEND_BASE}/api/v1/persons", verify=False)
    return res.json()

@app.get("/api/v1/history", tags=["Detection"])
def get_history():
    """Gets the last 5 detection records (In-Memory Array)."""
    res = requests.get(f"{C_BACKEND_BASE}/api/v1/history", verify=False)
    return res.json()

@app.post("/api/v1/command", tags=["System"])
def send_command(payload: CommandRequest):
    """Sends a system command (e.g., {"cmd": "reboot"})."""
    res = requests.post(f"{C_BACKEND_BASE}/api/v1/command", json=payload.dict(), verify=False)
    return res.json() 

# =======================
# PART 4 ENDPOINTS ADDED
# =======================

@app.get("/api/v1/total_detections", tags=["Blackbox"])
def get_total_detections():
    """Gets the total historical person detections stored in the SQLite database."""
    res = requests.get(f"{C_BACKEND_BASE}/api/v1/total_detections", verify=False)
    return res.json()

@app.post("/api/v1/guard", tags=["System"])
def toggle_guard_mode(payload: GuardRequest):
    """Toggles Guard Mode on (1) or off (0) to enable instant alerts and emergency MQTT topics."""
    res = requests.post(f"{C_BACKEND_BASE}/api/v1/guard", json=payload.dict(), verify=False)
    return res.json()

@app.post("/api/v1/heartbeat", tags=["System"])
def send_heartbeat():
    """Pings the Software Watchdog to indicate the video processor is alive."""
    res = requests.post(f"{C_BACKEND_BASE}/api/v1/heartbeat", verify=False)
    return res.json()
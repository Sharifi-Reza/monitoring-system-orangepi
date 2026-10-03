import cv2
import time
import datetime
import requests
import urllib3
import threading
from flask import Flask, Response

# Suppress insecure HTTPS warnings
urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

app = Flask(__name__)
camera = cv2.VideoCapture(0)

# Load lightweight Haar Cascade Face Detector
face_cascade = cv2.CascadeClassifier(cv2.data.haarcascades + 'haarcascade_frontalface_default.xml')

# Replace with your Orange Pi's IP
ORANGE_PI_IP = "10.242.176.54"

# Part 4 Global Flags & Thread Locks
THROTTLE_MODE = False
latest_frame = None
frame_lock = threading.Lock()

def system_monitor():
    """Part 4: Adaptive Thermal Management Thread. Polls server for throttle flag."""
    global THROTTLE_MODE
    while True:
        try:
            res = requests.get(f"https://{ORANGE_PI_IP}:8443/api/v1/telemetry", verify=False, timeout=3.0)
            if res.status_code == 200:
                data = res.json()
                # If thermal throttle is 1, turn on THROTTLE_MODE to save Pi resources
                THROTTLE_MODE = (data.get("thermal_throttle", 0) == 1)
        except Exception:
            pass
        time.sleep(5)

def send_heartbeat():
    """Part 4: Watchdog thread. Sends alive ping to Orange Pi every 10 seconds."""
    while True:
        try:
            requests.post(f"https://{ORANGE_PI_IP}:8443/api/v1/heartbeat", verify=False, timeout=2.0)
        except Exception:
            pass
        time.sleep(10)

def notify_orange_pi(count):
    """Runs in a background thread so it doesn't block the video stream."""
    try:
        print(f"[Debug] Sending count {count} to Orange Pi...")
        res = requests.post(f"https://{ORANGE_PI_IP}:8443/api/v1/update_persons", 
                            json={"count": count}, verify=False, timeout=3.0)
        print(f"[Debug] Orange Pi replied: {res.status_code}")
    except Exception as e:
        print(f"[Error] Failed to notify Orange Pi: {e}")

def generate_frames():
    global THROTTLE_MODE, latest_frame
    prev_time = time.time()
    last_reported_count = -1
    frame_counter = 0
    cached_boxes = []

    while True:
        success, frame = camera.read()
        if not success:
            # If the camera drops, break the loop to allow Flask to recover
            break

        frame_counter += 1
        
        # Resize for output stream
        frame_resized = cv2.resize(frame, (640, 480))
        
        # Part 4: Thermal Adaptive FPS. Skip more frames if Pi is overheating.
        skip_rate = 10 if THROTTLE_MODE else 3

        if frame_counter % skip_rate == 0:
            gray = cv2.cvtColor(frame_resized, cv2.COLOR_BGR2GRAY)
            small_gray = cv2.resize(gray, (320, 240))
            faces = face_cascade.detectMultiScale(small_gray, scaleFactor=1.1, minNeighbors=5, minSize=(15, 15))
            cached_boxes = [(x*2, y*2, w*2, h*2) for (x, y, w, h) in faces]

        person_count = len(cached_boxes)

        for (x, y, w, h) in cached_boxes:
            cv2.rectangle(frame_resized, (x, y), (x + w, y + h), (0, 255, 0), 2)
            cv2.putText(frame_resized, "Person", (x, y - 10), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 0), 2)

        curr_time = time.time()
        fps = 1.0 / (curr_time - prev_time) if (curr_time - prev_time) > 0 else 0.0
        prev_time = curr_time

        now_str = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        
        # Add visual Throttle indicator to Stream
        color = (0, 0, 255) if THROTTLE_MODE else (0, 255, 255)
        throttle_txt = " | THROTTLED" if THROTTLE_MODE else ""
        
        cv2.putText(frame_resized, f"ID: 401101932 | FPS: {fps:.1f}{throttle_txt}", (10, 25), cv2.FONT_HERSHEY_SIMPLEX, 0.6, color, 2)
        cv2.putText(frame_resized, f"Time: {now_str} | Persons: {person_count}", (10, 50), cv2.FONT_HERSHEY_SIMPLEX, 0.6, color, 2)

        if person_count != last_reported_count:
            threading.Thread(target=notify_orange_pi, args=(person_count,), daemon=True).start()
            last_reported_count = person_count

        ret, buffer = cv2.imencode('.jpg', frame_resized)
        frame_bytes = buffer.tobytes()

        # Thread-safe write to global frame for snapshot route
        with frame_lock:
            latest_frame = frame_bytes

        yield (b'--frame\r\n'
               b'Content-Type: image/jpeg\r\n\r\n' + frame_bytes + b'\r\n')

@app.route('/video_feed')
def video_feed():
    return Response(generate_frames(), mimetype='multipart/x-mixed-replace; boundary=frame')

@app.route('/snapshot')
def snapshot():
    # Thread-safe read from the cached global frame
    with frame_lock:
        if latest_frame is not None:
            return Response(latest_frame, mimetype='image/jpeg')
    return "Failed", 500

if __name__ == '__main__':
    # Start Part 4 Background Threads
    threading.Thread(target=system_monitor, daemon=True).start()
    threading.Thread(target=send_heartbeat, daemon=True).start()
    app.run(host='0.0.0.0', port=5000, threaded=True)
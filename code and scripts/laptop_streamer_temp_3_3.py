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
ORANGE_PI_IP = "10.55.228.31"

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
# CHANGE THESE TWO NUMBERS FOR EACH TEST:
        TEST_WIDTH = 1280
        TEST_HEIGHT = 720
        
        # Resize for output stream and detection
        frame_resized = cv2.resize(frame, (TEST_WIDTH, TEST_HEIGHT))
        
        # FRAME SKIPPING: Run detection only every 3rd frame
        if frame_counter % 3 == 0:
            gray = cv2.cvtColor(frame_resized, cv2.COLOR_BGR2GRAY)
            
            # Run detection on the exact test resolution
            faces = face_cascade.detectMultiScale(gray, scaleFactor=1.1, minNeighbors=5, minSize=(15, 15))
            
            # No need to scale boxes up anymore
            cached_boxes = [(x, y, w, h) for (x, y, w, h) in faces]

        person_count = len(cached_boxes)

        # Draw Bounding Boxes from cache
        for (x, y, w, h) in cached_boxes:
            cv2.rectangle(frame_resized, (x, y), (x + w, y + h), (0, 255, 0), 2)
            cv2.putText(frame_resized, "Person", (x, y - 10), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 0), 2)

        # Calculate smooth FPS
        curr_time = time.time()
        fps = 1.0 / (curr_time - prev_time) if (curr_time - prev_time) > 0 else 0.0
        prev_time = curr_time

        # Overlays: ID, Timestamp, Count, FPS
        now_str = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        cv2.putText(frame_resized, f"ID: 401101932 | FPS: {fps:.1f}", (10, 25), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 255), 2)
        cv2.putText(frame_resized, f"Time: {now_str} | Persons: {person_count}", (10, 50), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 255), 2)

        # Notify Orange Pi if count changes using a BACKGROUND THREAD
        if person_count != last_reported_count:
            threading.Thread(target=notify_orange_pi, args=(person_count,), daemon=True).start()
            last_reported_count = person_count

        # Encode frame
        ret, buffer = cv2.imencode('.jpg', frame_resized)
        frame_bytes = buffer.tobytes()

        yield (b'--frame\r\n'
               b'Content-Type: image/jpeg\r\n\r\n' + frame_bytes + b'\r\n')

@app.route('/video_feed')
def video_feed():
    return Response(generate_frames(), mimetype='multipart/x-mixed-replace; boundary=frame')

@app.route('/snapshot')
def snapshot():
    success, frame = camera.read()
    if success:
        frame_resized = cv2.resize(frame, (640, 480))
        ret, buffer = cv2.imencode('.jpg', frame_resized)
        return Response(buffer.tobytes(), mimetype='image/jpeg')
    return "Failed", 500

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, threaded=True)
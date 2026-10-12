#!/usr/bin/env python3
import json, time, logging, cv2, numpy as np, torch, smtplib, threading, os
from datetime import datetime, timezone
from threading import Lock
from email.mime.text import MIMEText
from email.mime.multipart import MIMEMultipart
from flask import Flask, Response, render_template, jsonify, request, send_from_directory
from flask_cors import CORS
from gpiozero import LED
from gpiozero.pins.lgpio import LGPIOFactory

try:
    from ultralytics import YOLO
    ULTRALYTICS_AVAILABLE = True
except Exception:
    ULTRALYTICS_AVAILABLE = False

# ===================== CONFIGURATION =====================
MODEL_PATH = "best.pt"
CAMERA_INDEX = 0
TARGET_CLASSES = {"fire", "smoke", "light_smoke"}
CONFIDENCE_THRESHOLD = 0.35
ESP_SIGNAL_PIN = 17

FIRE_DEBOUNCE_FRAMES = 3  
SIGNAL_COOLDOWN_SEC = 5.0 
FRAME_WIDTH, FRAME_HEIGHT = 1280, 720
MJPEG_BOUNDARY = "frame"
FLIP_CAMERA = True 

EMAIL_SENDER = "robosapiensai1@gmail.com"
EMAIL_PASSWORD = "ormn ugdd uwlf vgmo"
EMAIL_RECEIVER = "atrayudethe@gmail.com"
SMTP_SERVER = "smtp.gmail.com"
SMTP_PORT = 587
EMAIL_COOLDOWN_SEC = 300 

# ===================== INITIALIZATION =====================
logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")
logger = logging.getLogger("fire_ranger")

frontend_dir = "frontend"
app = Flask(__name__, template_folder=frontend_dir, static_folder=frontend_dir, static_url_path='')
CORS(app)

# Initialize physical Raspberry Pi GPIO
factory = LGPIOFactory()
signal = LED(ESP_SIGNAL_PIN, pin_factory=factory)
signal.off()

_signal_lock = Lock()
_last_detection_time = 0.0
_signal_forced = False
_last_email_time = 0.0
latest_report = {}
master_report = {}
reports_history = []
master_history = []

# ===================== CORE LOGIC =====================
@app.before_request
def _capture_incoming_reports():
    try:
        if request.method == "POST":
            path = request.path or ""
            if path.rstrip("/") == "/report" or path.rstrip("/") == "/master":
                data = request.get_json(silent=True)
                if data is not None:
                    entry = {
                        "ts": datetime.now(timezone.utc).astimezone().isoformat(timespec='seconds'),
                        "path": path.rstrip("/"),
                        "data": data
                    }
                    if path.rstrip("/") == "/report":
                        reports_history.append(entry)
                        global latest_report
                        latest_report = data
                    else:
                        master_history.append(entry)
                        global master_report
                        master_report = data
    except Exception:
        logger.exception("Error capturing incoming report")

def send_email_alert(label, conf):
    global _last_email_time
    now = time.time()
    if now - _last_email_time < EMAIL_COOLDOWN_SEC: return
    _last_email_time = now
    def _send():
        try:
            msg = MIMEMultipart()
            msg['From'] = EMAIL_SENDER
            msg['To'] = EMAIL_RECEIVER
            msg['Subject'] = f"URGENT: {label.upper()} Detected by FireGuardX!"
            body = f"System Alert!\n\nDetected: {label}\nConfidence: {conf:.2f}\nTime: {now_iso()}\n\nPlease check the live dashboard immediately."
            msg.attach(MIMEText(body, 'plain'))
            server = smtplib.SMTP(SMTP_SERVER, SMTP_PORT)
            server.starttls()
            server.login(EMAIL_SENDER, EMAIL_PASSWORD)
            server.send_message(msg)
            server.quit()
            logger.info("✅ Email Alert Sent Successfully!")
        except Exception as e:
            logger.error(f"Failed to send email: {e}")
    threading.Thread(target=_send, daemon=True).start()

def load_model(path):
    if ULTRALYTICS_AVAILABLE: return YOLO(path)
    else: return torch.hub.load('ultralytics/yolov8', 'custom', path=path)

print("🧠 Loading YOLO AI Model...")
model = load_model(MODEL_PATH)
print("✅ Model Loaded!")

camera = cv2.VideoCapture(CAMERA_INDEX)
camera.set(cv2.CAP_PROP_FRAME_WIDTH, FRAME_WIDTH)
camera.set(cv2.CAP_PROP_FRAME_HEIGHT, FRAME_HEIGHT)
time.sleep(0.5)

def now_iso(): return datetime.now(timezone.utc).astimezone().isoformat(timespec='seconds')

def set_signal_high():
    global _last_detection_time
    with _signal_lock:
        if not _signal_forced:
            signal.on()
            _last_detection_time = time.time()

def set_signal_low():
    with _signal_lock:
        if not _signal_forced: signal.off()

def maybe_clear_signal_by_cooldown():
    global _last_detection_time
    with _signal_lock:
        if signal.is_lit and not _signal_forced:
            if time.time() - _last_detection_time >= SIGNAL_COOLDOWN_SEC:
                signal.off()

def parse_results(results):
    detections = []
    try:
        r = results[0] if isinstance(results, (list, tuple)) else results
        if hasattr(r, "boxes"):
            boxes = r.boxes
            if boxes is None: return detections
            xyxy = boxes.xyxy.cpu().numpy()
            confs = boxes.conf.cpu().numpy()
            cls_ids = boxes.cls.cpu().numpy().astype(int)
            names = getattr(model, "names", {}) or {}
            for (x1, y1, x2, y2), conf, cid in zip(xyxy, confs, cls_ids):
                label = names.get(int(cid), str(int(cid)))
                detections.append((label, float(conf), int(x1), int(y1), int(x2), int(y2)))
            return detections
    except Exception: pass
    return detections

def get_pi_temperature():
    try:
        with open("/sys/class/thermal/thermal_zone0/temp", "r") as f:
            return round(int(f.read().strip()) / 1000.0, 1)
    except Exception: return None

def generate_frames():
    global _last_detection_time
    consecutive_fire_frames = 0  
        
    while True:
        ok, frame = camera.read()
        if not ok: time.sleep(0.1); continue
                
        if FLIP_CAMERA: frame = cv2.flip(frame, -1) 
                
        try: results = model(frame)
        except Exception: continue
                
        detections = parse_results(results)
        fire_in_current_frame = False
                
        for label, conf, x1, y1, x2, y2 in detections:
            if conf < CONFIDENCE_THRESHOLD: continue
            color = (0, 0, 255) if label in TARGET_CLASSES else (0, 255, 0)
            cv2.rectangle(frame, (x1, y1), (x2, y2), color, 2)
            cv2.putText(frame, f"{label} {conf:.2f}", (x1 + 3, y1 - 4), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 1)
                        
            if label in TARGET_CLASSES: 
                fire_in_current_frame = True
                send_email_alert(label, conf) 
                         
        if fire_in_current_frame:
            consecutive_fire_frames += 1
            if consecutive_fire_frames >= FIRE_DEBOUNCE_FRAMES:
                set_signal_high()
        else:
            consecutive_fire_frames = 0
            maybe_clear_signal_by_cooldown()
                
        ret, buf = cv2.imencode('.jpg', frame, [int(cv2.IMWRITE_JPEG_QUALITY), 80])
        if not ret: continue
        yield (b'--' + MJPEG_BOUNDARY.encode() + b'\r\nContent-Type:image/jpeg\r\n\r\n' + buf.tobytes() + b'\r\n')

# ===================== FRONTEND ROUTES =====================
@app.route("/")
@app.route("/dashboard.html")
def index(): return render_template("dashboard.html")

@app.route("/reports.html")
def reports_page(): return render_template("reports.html")

@app.route("/alerts.html")
def alerts_page(): return render_template("alerts.html")

@app.route("/about.html")
def about_page(): return render_template("about.html")

@app.route("/css/<path:filename>")
def serve_css(filename):
    return send_from_directory(os.path.join(frontend_dir, "css"), filename)

@app.route("/js/<path:filename>")
def serve_js(filename):
    return send_from_directory(os.path.join(frontend_dir, "js"), filename)

# ===================== API ROUTES =====================
@app.route("/video")
def video_feed():
    return Response(generate_frames(), mimetype=f"multipart/x-mixed-replace; boundary={MJPEG_BOUNDARY}")

@app.route("/status", strict_slashes=False)
def status():
    with _signal_lock:
        return jsonify({
            "signal_high": bool(signal.is_lit),
            "pi_temperature_c": get_pi_temperature()
        })

@app.route("/report", methods=["POST", "GET"], strict_slashes=False)
def handle_report():
    if request.method == "POST": return jsonify({"status": "ok"})
    return jsonify(latest_report)

@app.route("/master", methods=["POST", "GET"], strict_slashes=False)
def handle_master():
    if request.method == "POST": return jsonify({"status": "ok"})
    return jsonify(master_report)

@app.route("/bulk_report", methods=["POST"], strict_slashes=False)
def handle_bulk_report():
    raw_data = request.get_data(as_text=True)
    lines = raw_data.strip().split('\n')
    added_count = 0
    for line in lines:
        if not line.strip(): continue
        try:
            payload = json.loads(line)
            entry = {
                "ts": datetime.now(timezone.utc).astimezone().isoformat(timespec='seconds'),
                "path": "/report",
                "data": payload
            }
            reports_history.append(entry)
            added_count += 1
        except Exception as e: logger.error(f"Failed to parse line: {e}")
                
    logger.info(f"📥 Received {added_count} offline reports from LittleFS!")
    return jsonify({"status": "bulk_processed", "count": added_count}), 200

@app.route("/report/history", methods=["GET"], strict_slashes=False)
def get_report_history(): return jsonify(reports_history)

@app.route("/master/history", methods=["GET"], strict_slashes=False)
def get_master_history(): return jsonify(master_history)

@app.route("/override/<action>", methods=["POST"], strict_slashes=False)
def override_signal(action):
    global _signal_forced
    with _signal_lock:
        if action == "on":
            _signal_forced = True; signal.on()
        elif action == "off":
            _signal_forced = False; signal.off()
    return jsonify({"status": f"forced_{action}"})

@app.route("/move/<direction>", methods=["POST"], strict_slashes=False)
def handle_movement(direction):
    valid_directions = ["forward", "backward", "left", "right", "stop"]
    if direction in valid_directions:
        logger.info(f"🕹️ Manual Command Logged for Future Hardware: Robot moving {direction.upper()}")
        return jsonify({"status": "success", "direction": direction}), 200
    return jsonify({"status": "error", "message": "Invalid direction"}), 400

def cleanup():
    try: camera.release()
    except: pass

if __name__ == "__main__":
    try:
        print("\n=======================================================")
        print("🔥 FIREGUARDX PI SERVER ONLINE 🔥")
        print("💻 ALL SYSTEMS GO. OPEN YOUR BROWSER TO PORT 5000!")
        print("=======================================================\n")
        app.run(host="0.0.0.0", port=5000, threaded=True)
    finally: cleanup()
#!/usr/bin/env python3

import subprocess
import os
from gpiozero import Button
from signal import pause

# ===================== CONFIGURATION =====================
# Change this to whatever GPIO pin your Touch Sensor's SIGNAL wire is connected to
TOUCH_SENSOR_PIN = 27  

# Paths based on your previous code
VENV_PYTHON = "/home/astraliesdethe/fire_ranger/bin/python"
APP2_PATH = "/home/astraliesdethe/app2.py"
FRONTEND_DIR = "/home/astraliesdethe/frontend"

# Global variables to keep track of running processes
backend_process = None
frontend_process = None
is_running = False

def toggle_system():
    global backend_process, frontend_process, is_running
    
    if not is_running:
        print("👆 Touch detected! Launching FireGuardX System...")
        
        # 1. Launch the YOLO AI Backend (app2.py)
        backend_process = subprocess.Popen([VENV_PYTHON, APP2_PATH])
        
        # 2. Launch the HTTP Server for the dashboard
        # We use 'cwd' to ensure it serves files from the frontend directory
        frontend_process = subprocess.Popen(
            [VENV_PYTHON, "-m", "http.server", "8080"], 
            cwd=FRONTEND_DIR
        )
        
        is_running = True
        print("✅ System is fully online! (Touch again to shut down)")
        
    else:
        print("👆 Touch detected! Shutting down FireGuardX System...")
        
        # Terminate the running processes safely
        if backend_process:
            backend_process.terminate()
        if frontend_process:
            frontend_process.terminate()
            
        is_running = False
        print("🛑 System offline. Waiting for touch to restart...")

# ===================== SENSOR SETUP =====================
print(f"📡 Launcher waiting on GPIO {TOUCH_SENSOR_PIN}...")

# Initialize the touch sensor as a button
# If your sensor triggers randomly, change pull_up=False to pull_up=True
sensor = Button(TOUCH_SENSOR_PIN, pull_up=False, bounce_time=0.5)

# When touched, run the toggle function
sensor.when_pressed = toggle_system

# Keep the script running forever
pause()

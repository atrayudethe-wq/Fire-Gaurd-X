#include <Arduino.h>
#include <ESP32Servo.h>

// ===================== PIN ASSIGNMENTS =====================
#define SERVO_PIN         19   
#define HEARTBEAT_PIN      2   
#define STOP_PIN           4   
#define EXT_PIN           15   

// ===================== SERVO TUNING =====================
const int SWEEP_MIN = 0;              
const int SWEEP_MAX = 270;            
const int MICRO_STEP_DEG = 1;         
// Tuned to 150ms for a 3 FPS camera (approx 2.2 degrees of movement per frame)
const unsigned long MICRO_STEP_DELAY_MS = 150; 
const unsigned long TELEMETRY_INTERVAL_MS = 250; 

Servo sweepServo;

// Start variables at 0 for the boot sequence
volatile int servoAngle = 0;
int sweepPos = 0;
int sweepDir = 1;         
bool servoStopped = false;
bool sweepCycleActive = true; 

unsigned long lastHeartbeat = 0;
unsigned long lastTelemetrySend = 0;
unsigned long lastSweepMove = 0;

String angleToCardinal(int angle) {
  if (angle >= 337 || angle < 22) return "N";
  if (angle >= 22 && angle < 67) return "NE";
  if (angle >= 67 && angle < 112) return "E";
  if (angle >= 112 && angle < 157) return "SE";
  if (angle >= 157 && angle < 202) return "S";
  if (angle >= 202 && angle < 247) return "SW";
  if (angle >= 247 && angle < 292) return "W";
  return "NW";
}

void writeServo270(int angle270) {
  angle270 = constrain(angle270, 0, 270);
  // Maps standard 0-180 logic to a 270-degree physical servo
  int pulseWidth = map(angle270, 0, 270, 500, 2500);
  sweepServo.writeMicroseconds(pulseWidth);
}

void setup() {
  Serial.begin(115200);

  pinMode(HEARTBEAT_PIN, OUTPUT);
  pinMode(STOP_PIN, INPUT_PULLUP);
  pinMode(EXT_PIN, INPUT_PULLDOWN);

  sweepServo.setPeriodHertz(50);
  sweepServo.attach(SERVO_PIN, 500, 2500);
  
  // ==========================================
  // HARDWARE ALIGNMENT BOOT SEQUENCE
  // ==========================================
  writeServo270(0); // Instantly snap to the absolute 0-degree position
  delay(5000);      // Hold completely still for 5 seconds to allow physical mounting
  // ==========================================
}

void loop() {
  unsigned long now = millis();

  // 1. Hardware Heartbeat (Blinks every 500ms)
  if (now - lastHeartbeat >= 500) {
    digitalWrite(HEARTBEAT_PIN, !digitalRead(HEARTBEAT_PIN));
    lastHeartbeat = now;
  }

  // 2. Hardware Fire Signal Check (From Pi GPIO 17)
  if (digitalRead(EXT_PIN) == HIGH) {
    sweepCycleActive = false; 
  } else {
    sweepCycleActive = true;  
  }

  // 3. Physical Emergency Stop Override
  if (digitalRead(STOP_PIN) == LOW) {
    if (!servoStopped) {
      sweepServo.detach();
      servoStopped = true;
    }
  } else {
    if (servoStopped) {
      sweepServo.setPeriodHertz(50);
      sweepServo.attach(SERVO_PIN, 500, 2500);
      writeServo270(servoAngle);
      servoStopped = false;
    }

    // 4. Non-Blocking 270-Degree Sweep
    if (sweepCycleActive && !servoStopped && (now - lastSweepMove >= MICRO_STEP_DELAY_MS)) {
      lastSweepMove = now;
      sweepPos += (sweepDir * MICRO_STEP_DEG);

      if (sweepPos >= SWEEP_MAX) {
        sweepPos = SWEEP_MAX;
        sweepDir = -1;
      } else if (sweepPos <= SWEEP_MIN) {
        sweepPos = SWEEP_MIN;
        sweepDir = 1;
      }

      writeServo270(sweepPos);
      servoAngle = sweepPos;
    }
  }

  // 5. USB Serial Telemetry (Runs continuously)
  if (now - lastTelemetrySend >= TELEMETRY_INTERVAL_MS) {
    lastTelemetrySend = now;
    float heading = (float)servoAngle;
    String dir = angleToCardinal(servoAngle);

    Serial.print("{\"heading\":");
    Serial.print(heading, 1);
    Serial.print(",\"direction\":\"");
    Serial.print(dir);
    Serial.print("\",\"servoAngle\":");
    Serial.print(servoAngle);
    Serial.println("}");
  }
}
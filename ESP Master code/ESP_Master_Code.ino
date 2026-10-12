/*
  ===========================================================================
  MASTER ESP32 - FIREFIGHTING ROBOT CONTROLLER (IOT GATEWAY VERSION)
  ===========================================================================
  Updates Applied:
  - Memory Safe: ESP-NOW callback only sets a flag. HTTP POST moved to loop().
  - Wi-Fi Fallback: Defaults to Channel 1 if router connection fails.
  - Servo Fix: Uses writeMicroseconds for true 270-degree physical sweep.
  - Cleaned Serial: Removed heavy serial prints to prevent core blocking.
  ===========================================================================
*/

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h> 
#include <Wire.h>
#include <Adafruit_HMC5883_U.h>
#include <HTTPClient.h>
#include <ESP32Servo.h>
#include <LittleFS.h>
#include <WebServer.h>

// ===================== Hardware Pin Definitions =====================
#define SERVO_PIN         19   
#define HEARTBEAT_PIN      2   
#define STOP_PIN           4   
#define EXT_PIN           15   
#define SDA_PIN           21   
#define SCL_PIN           22   

// ===================== Network & API Configuration =====================
const char* WIFI_SSID = "Ginny";
const char* WIFI_PASS = "ginny123456789";

String piReportURL = "http://192.168.29.215:5000/report";
String piMasterURL = "http://192.168.29.215:5000/master";
String piBulkURL   = "http://192.168.29.215:5000/bulk_report"; 

uint8_t slaveMAC[] = {0xE4, 0x65, 0xB8, 0xE7, 0x36, 0x10};

WebServer server(8080);

// ===================== Servo Sweep Configuration =====================
const int SWEEP_MIN = 0;              
const int SWEEP_MAX = 270;            
const int MICRO_STEP_DEG = 1;         
const unsigned long MICRO_STEP_DELAY_MS = 15; 

// ===================== Timing Intervals (Milliseconds) =====================
const unsigned long MASTER_POST_INTERVAL = 2000; 
const unsigned long COMMAND_INTERVAL = 6000;     
const unsigned long HMC_SEND_INTERVAL = 2000;    

// ===================== Global Variables & Data Structures =====================
Adafruit_HMC5883_Unified mag = Adafruit_HMC5883_Unified(12345);
Servo sweepServo;

typedef struct struct_message {
  float heading;
  char direction[4];
  int servoAngle;
  char command[16];
  float temp;
  float hum;
  int mqValue;
  int distance;
  int flameStatus;
} __attribute__((packed)) struct_message;

struct_message outgoingData; 
struct_message incomingData; 

// --- Core State Variables ---
volatile bool newSlaveDataReady = false; // FLAG FOR MEMORY SAFETY
volatile int servoAngle = 135;
volatile int sweepPos = 0;
volatile int sweepDir = 1;         
volatile bool servoStopped = false;
volatile bool sweepCycleActive = true;
volatile bool slaveReportsNoWater = false;
volatile bool lastAckReceived = false;

// --- Timers & Debounce ---
unsigned long lastMasterPost = 0;
unsigned long lastCommandSend = 0;
unsigned long lastHeartbeat = 0;
unsigned long lastHmcSend = 0;
int lastExtPinState = LOW;
unsigned long lastExtDebounce = 0;
const unsigned long EXT_DEBOUNCE_MS = 50; 
bool extRisingEdgeDetected = false;

TaskHandle_t servoTaskHandle;

// ===================== Forward Declarations =====================
void processIncomingData();
void sendToPiReport();
void writeServo270(int angle270);
void sendCommandToSlave(const char* cmd, float heading, const String &dir, int angle);
void sendToPiMaster(float heading, const String &dir, int currentServoAngle);
void saveToCache(String jsonPayload);
void uploadCacheToPi();
void handleTuningRequest();

// ===================== ESP-NOW Callbacks =====================
// WARNING: Keep this absolute minimum. NO Serial prints, NO HTTP requests.
void OnDataRecv(const esp_now_recv_info *info, const uint8_t *data, int len) {
  if (len == sizeof(incomingData)) {
    memcpy((void*)&incomingData, data, sizeof(incomingData));
    newSlaveDataReady = true; // Tell the main loop to handle it safely
  }
}

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) { }

// ===================== Hardware Helper Functions =====================

float getHeading() {
  sensors_event_t event;
  mag.getEvent(&event);
  float heading = atan2(event.magnetic.y, event.magnetic.x);
  if (heading < 0) heading += 2 * PI; 
  return heading * 180.0 / M_PI; 
}

String headingToCardinal(float heading) {
  if (heading >= 337.5 || heading < 22.5) return "N";
  if (heading >= 22.5 && heading < 67.5) return "NE";
  if (heading >= 67.5 && heading < 112.5) return "E";
  if (heading >= 112.5 && heading < 157.5) return "SE";
  if (heading >= 157.5 && heading < 202.5) return "S";
  if (heading >= 202.5 && heading < 247.5) return "SW";
  if (heading >= 247.5 && heading < 292.5) return "W";
  return "NW";
}

// Fixed 270-degree servo mapping using microsecond pulses
void writeServo270(int angle270) {
  angle270 = constrain(angle270, 0, 270);
  int pulseWidth = map(angle270, 0, 270, 500, 2500);
  sweepServo.writeMicroseconds(pulseWidth);
}

// ===================== DUAL-CORE TASK: SERVO CONTROL =====================

void servoControlTask(void * pvParameters) {
  for(;;) { 
    if (digitalRead(STOP_PIN) == HIGH) {
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

      if (sweepCycleActive && !servoStopped) {
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
    vTaskDelay(MICRO_STEP_DELAY_MS / portTICK_PERIOD_MS); 
  }
}

// ===================== Web Tuning =====================
void handleTuningRequest() {
  if (server.hasArg("setting") && server.hasArg("value")) {
    String setting = server.arg("setting"); 
    int val = server.arg("value").toInt();  
    
    String cmdStr = "TUNE_" + setting;
    cmdStr.toUpperCase();
    
    sendCommandToSlave(cmdStr.c_str(), getHeading(), headingToCardinal(getHeading()), val);
    server.send(200, "text/plain", "Success");
  } else {
    server.send(400, "text/plain", "Missing args");
  }
}

// ===================== Offline Caching =====================
void saveToCache(String jsonPayload) {
  File file = LittleFS.open("/cache.jsonl", FILE_APPEND);
  if (file) {
    file.println(jsonPayload); 
    file.close();
  }
}

void uploadCacheToPi() {
  if (!LittleFS.exists("/cache.jsonl")) return; 

  File file = LittleFS.open("/cache.jsonl", FILE_READ);
  if (!file) return;
  
  String bulkData = file.readString();
  file.close();

  HTTPClient http;
  http.begin(piBulkURL);
  http.addHeader("Content-Type", "application/x-ndjson"); 
  int httpCode = http.POST(bulkData);
  http.end();

  if (httpCode == 200 || httpCode == 201) {
    LittleFS.remove("/cache.jsonl"); 
  }
}

// ===================== Transmission Functions =====================

void sendCommandToSlave(const char* cmd, float heading, const String &dir, int angle) {
  String cmdStr = String(cmd);
  cmdStr.toUpperCase();

  if (cmdStr == "EXTINGUISH" || cmdStr == "FIRE" || cmdStr == "EXT" || cmdStr == "FIRE_EXT") {
    if (slaveReportsNoWater) return;
    if (digitalRead(EXT_PIN) != HIGH) return;
  }

  outgoingData.heading = heading;
  memset(outgoingData.direction, 0, sizeof(outgoingData.direction));
  strncpy(outgoingData.direction, dir.c_str(), sizeof(outgoingData.direction)-1);
  outgoingData.servoAngle = angle;
  memset(outgoingData.command, 0, sizeof(outgoingData.command));
  strncpy(outgoingData.command, cmd, sizeof(outgoingData.command)-1);
  
  esp_now_send(slaveMAC, (uint8_t *)&outgoingData, sizeof(outgoingData));
}

// Safe function called in main loop, NOT in interrupt
void processIncomingData() {
  String cmd = String(incomingData.command);
  cmd.trim();
  cmd.toUpperCase();

  if (cmd == "NOWATER") {
    slaveReportsNoWater = true;
  } else if (cmd == "ACK_EXT") {
    lastAckReceived = true;
  } else {
    slaveReportsNoWater = false;
  }

  if (cmd == "FIRE" || cmd == "EXTINGUISH" || cmd == "FIRE_EXT" || cmd == "EXT") {
    sweepCycleActive = true; 
  }

  sendToPiReport(); // Safely send HTTP data now
}

void sendToPiReport() {
  String json = "{";
  json += "\"heading\":" + String(incomingData.heading, 2) + ",";
  json += "\"direction\":\"" + String(incomingData.direction) + "\",";
  json += "\"servoAngle\":" + String(incomingData.servoAngle) + ",";
  json += "\"command\":\"" + String(incomingData.command) + "\",";
  json += "\"temp\":" + String(incomingData.temp, 2) + ",";
  json += "\"humidity\":" + String(incomingData.hum, 2) + ",";
  json += "\"mqValue\":" + String(incomingData.mqValue) + ",";
  json += "\"distance\":" + String(incomingData.distance) + ",";
  json += "\"flame\":" + String(incomingData.flameStatus);
  json += "}";

  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(piReportURL);
    http.addHeader("Content-Type", "application/json");
    int httpCode = http.POST(json);
    http.end();

    if (httpCode > 0) {
      uploadCacheToPi(); 
    } else {
      saveToCache(json);
    }
  } else {
    saveToCache(json);
  }
}

void sendToPiMaster(float heading, const String &dir, int currentServoAngle) {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  http.begin(piMasterURL);
  http.addHeader("Content-Type", "application/json");

  String json = "{";
  json += "\"heading\":" + String(heading, 2) + ",";
  json += "\"direction\":\"" + dir + "\",";
  json += "\"servoAngle\":" + String(currentServoAngle);
  json += "}";

  http.POST(json);
  http.end();
}

// ===================== Standard Setup =====================

void setup() {
  LittleFS.begin(true);

  pinMode(HEARTBEAT_PIN, OUTPUT);
  pinMode(STOP_PIN, INPUT_PULLDOWN); 
  pinMode(EXT_PIN, INPUT_PULLDOWN);  

  Wire.begin(SDA_PIN, SCL_PIN);
  mag.begin();

  sweepServo.setPeriodHertz(50);
  sweepServo.attach(SERVO_PIN, 500, 2500);
  writeServo270(0);

  // --- WiFi and ESP-NOW Setup with Fallback Logic ---
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) {
    delay(500);
  }
  
  int32_t channel = 1; // Default fallback channel
  
  if (WiFi.status() == WL_CONNECTED) {
    channel = WiFi.channel();
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE); 
  } else {
    // WiFi Failed. Set hardware to fallback channel so ESP-NOW still works
    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE); 
  }

  server.on("/tune", HTTP_POST, handleTuningRequest); 
  server.on("/tune", HTTP_GET, handleTuningRequest);  
  server.begin();

  if (esp_now_init() != ESP_OK) return;
  
  esp_now_register_recv_cb(reinterpret_cast<esp_now_recv_cb_t>(OnDataRecv));
  esp_now_register_send_cb(reinterpret_cast<esp_now_send_cb_t>(OnDataSent));

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, slaveMAC, 6);
  peerInfo.channel = channel;  
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);

  xTaskCreatePinnedToCore(servoControlTask, "ServoTask", 4096, NULL, 2, &servoTaskHandle, 0);
}

// ===================== Standard Loop (Core 1) =====================

void loop() {
  unsigned long now = millis();

  server.handleClient();

  if (now - lastHeartbeat > 500) {
    digitalWrite(HEARTBEAT_PIN, !digitalRead(HEARTBEAT_PIN));
    lastHeartbeat = now;
  }

  // Safely process data received from ESP-NOW
  if (newSlaveDataReady) {
    processIncomingData();
    newSlaveDataReady = false; 
  }

  int extReading = digitalRead(EXT_PIN);
  if (extReading != lastExtPinState) lastExtDebounce = now; 
  
  if ((now - lastExtDebounce) > EXT_DEBOUNCE_MS) {
    if (extReading == HIGH && lastExtPinState == LOW) extRisingEdgeDetected = true;
    lastExtPinState = extReading;
  }

  if (extRisingEdgeDetected) {
    extRisingEdgeDetected = false; 
    if (!slaveReportsNoWater) {
      lastAckReceived = false;
      sendCommandToSlave("EXTINGUISH", getHeading(), headingToCardinal(getHeading()), servoAngle);
    }
  }

  if (now - lastMasterPost >= MASTER_POST_INTERVAL) {
    lastMasterPost = now;
    sendToPiMaster(getHeading(), headingToCardinal(getHeading()), servoAngle);
  }

  if (now - lastCommandSend >= COMMAND_INTERVAL) {
    lastCommandSend = now;
    sendCommandToSlave("START", getHeading(), headingToCardinal(getHeading()), servoAngle);
  }

  if (now - lastHmcSend >= HMC_SEND_INTERVAL) {
    lastHmcSend = now;
    sendCommandToSlave("HMC", getHeading(), headingToCardinal(getHeading()), servoAngle);
  }

  delay(5); 
}
// ════════════════════════════════════════════════════════════════
//  1. CRITICAL CLOUD PROFILE DECLARATIONS (MUST BE FIRST)
// ════════════════════════════════════════════════════════════════
#define BLYNK_TEMPLATE_ID   "TMPL34YaDTWgj"
#define BLYNK_TEMPLATE_NAME "Plant Care System"

// ════════════════════════════════════════════════════════════════
//  2. SYSTEM LIBRARIES & DEPENDENCIES
// ════════════════════════════════════════════════════════════════
#include <DHT.h>
#include <BH1750.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>
#include <time.h>
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <esp_task_wdt.h> // ESP32 Hardware Watchdog System Engine

#include "secrets.h"

// Ensure fallback authentication token is handled cleanly
#ifndef BLYNK_AUTH_TOKEN
#define BLYNK_AUTH_TOKEN "AUTH" 
#endif

// ── HARDWARE PIN DEFINITIONS ─────────────────────────────────────
#define SERVO_PIN   26  // Drives the physical mechanical water valve gate
#define BUZZER_PIN  4
#define SOIL_PIN    34
#define WATER_PIN   35
#define DHT_PIN     15
#define SD_CS_PIN   5

// ── HARDWARE PWM SERVO ENGINE CONFIGURATION (ESP32 Core v3.x) ─────
#define SERVO_PWM_FREQ     50    // Standard analog/digital servo operational frequency (50Hz)
#define SERVO_PWM_RES      16    // 16-bit high-resolution duty cycle control depth

// 50Hz frequency frame = 20ms period. 16-bit resolution scale = 65535 total increments.
// SG90/MG996R standard calibration: 0.5ms pulse = 0 degrees, 2.5ms pulse = 180 degrees.
#define SERVO_MIN_DUTY     1638  // Counter-clockwise hard physical stop (0.5ms / 20ms * 65535) -> VALVE SHUT
#define SERVO_MAX_DUTY     8192  // Clockwise hard physical stop (2.5ms / 20ms * 65535) -> VALVE OPEN

// ── SYSTEM CONFIGURATION PARAMETERS ──────────────────────────────
#define WDT_TIMEOUT_SECONDS 10
const float ALPHA_FILTER    = 0.15; // Exponential Moving Average Filter Alpha

char ssid[] = WIFI_SSID;
char pass[] = WIFI_PASS;

// ── MULTI-CORE THREAD SYNCHRONIZATION ────────────────────────────
portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;

// ── SMOOTHED TELEMETRY REGISTERS (Mutex Protected) ────────────────
float soilMoisture = 45.0;
float temperature  = 26.5;
float humidity     = 60.0;
float lightLux     = 300.0;
int   waterLevel   = 80;
int   healthScore  = 78;
bool  valveStatus  = false; // Tracks physical position state of the servo valve gate

// ── STORAGE LOGGING CACHE BUFFER ──────────────────────────────────
float lastLoggedMoisture = -99.0;
float lastLoggedTemp     = -99.0;

// ── FAULT INTERRUPT FLAGS ─────────────────────────────────────────
bool valveLocked        = false; // System protective lock constraint flag
bool sensorFaultAlert   = false;
bool dhtFaultAlert      = false;
bool lightFaultAlert    = false;
bool waterFaultAlert    = false;
bool sdCardPresent      = false;

// ── TIMING CONTROL VARIABLES ──────────────────────────────────────
unsigned long valveStartTime = 0;
unsigned long lastFsmUpdate  = 0;
int  wateringAttempts        = 0;
#define MAX_VALVE_TIME       10000 // Hard safety cutoff window at 10 seconds
#define MAX_ATTEMPTS         3
float moistureBeforeWatering = 0;

// ── THRESHOLDS MANAGEMENT PROFILE ─────────────────────────────────
int   plantType   = 4;
float moistureMin = 30, moistureMax = 60;
float tempMin     = 15, tempMax     = 35;
float humidityMin = 30, humidityMax = 80;
float lightMin    = 1000, lightMax  = 80000;

// ── SCHEDULER STATE RECOVERY ──────────────────────────────────────
int  scheduleHour       = 7;
int  scheduleMinute     = 0;
bool scheduledWaterDone = false;
int  lastWateredDay     = -1;

DHT     dht(DHT_PIN, DHT22);
BH1750  lightMeter;

// Task Forward Declarations
void criticalCoreLoopTask(void * pvParameters);
void networkCommunicationTask(void * pvParameters);
void setThresholds();
void writeServoAngle(int angle);

// ════════════════════════════════════════════════════════════════
//  SETUP EXECUTOR
// ════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);

  // Initialize Hardware Watchdog Module using Modern v3.x API Structure
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = WDT_TIMEOUT_SECONDS * 1000,
    .idle_core_mask = (1 << portNUM_PROCESSORS) - 1, // Monitor both processing cores
    .trigger_panic = true
  };
  esp_task_wdt_init(&wdt_config);
  esp_task_wdt_add(NULL); // Subscribe main setup path to safety layer

  // Configure Hardware PWM for safe, precise servo movement control using modern v3.x syntax
  // ledcAttach automatically sets up the pin, frequency, and resolution in one call
  ledcAttach(SERVO_PIN, SERVO_PWM_FREQ, SERVO_PWM_RES);
  writeServoAngle(0); // Force valve into a secure, completely closed state immediately at startup

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  dht.begin();
  Wire.begin();
  if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
    lightFaultAlert = false;
  }
  sdCardPresent = SD.begin(SD_CS_PIN);

  setThresholds();

  // Spawning Multiprocessing Core Threads to split application layers
  xTaskCreatePinnedToCore(criticalCoreLoopTask, "Core1_FSM", 8192, NULL, 3, NULL, 1);     // Core 1: Industrial Automation
  xTaskCreatePinnedToCore(networkCommunicationTask, "Core0_Net", 4192, NULL, 1, NULL, 0); // Core 0: Asynchronous Communication

  esp_task_wdt_reset();
}

void loop() {
  vTaskDelete(NULL); // Terminate the generic loop to hand full execution over to FreeRTOS kernels
}

// ════════════════════════════════════════════════════════════════
//  CORE 1: DETACHED CRITICAL AUTOMATION ENGINE
// ════════════════════════════════════════════════════════════════
void criticalCoreLoopTask(void * pvParameters) {
  esp_task_wdt_add(NULL);
  
  for (;;) {
    esp_task_wdt_reset();
    unsigned long now = millis();

    // ── STEP 1: SENSOR SIGNAL SMOOTHING PROCESSING (EMA FILTER) ──
    int rawSoil = analogRead(SOIL_PIN);
    if (rawSoil > 10 && rawSoil < 4085) {
      float instantMoisture = map(rawSoil, 4095, 1500, 0, 100);
      portENTER_CRITICAL(&stateMux);
      soilMoisture = (ALPHA_FILTER * instantMoisture) + ((1.0 - ALPHA_FILTER) * soilMoisture);
      soilMoisture = constrain(soilMoisture, 0, 100);
      portEXIT_CRITICAL(&stateMux);
      sensorFaultAlert = false;
    } else {
      sensorFaultAlert = true;
    }

    float rawTemp = dht.readTemperature();
    float rawHum  = dht.readHumidity();
    if (!isnan(rawTemp) && !isnan(rawHum)) {
      portENTER_CRITICAL(&stateMux);
      temperature = rawTemp;
      humidity = rawHum;
      portEXIT_CRITICAL(&stateMux);
      dhtFaultAlert = false;
    } else {
      dhtFaultAlert = true;
    }

    float rawLux = lightMeter.readLightLevel();
    if (rawLux >= 0 && rawLux < 120000) {
      lightLux = rawLux;
      lightFaultAlert = false;
    } else {
      lightFaultAlert = true;
    }

    int rawWater = analogRead(WATER_PIN);
    float instantWater = map(rawWater, 0, 4095, 0, 100);
    waterLevel = (ALPHA_FILTER * instantWater) + ((1.0 - ALPHA_FILTER) * waterLevel);
    waterLevel = constrain(waterLevel, 0, 100);
    waterFaultAlert = (waterLevel <= 0);

    // ── STEP 2: DETERMINISTIC FINITE STATE WATERING AUTOMATION ────
    if (now - lastFsmUpdate >= 3000) {
      lastFsmUpdate = now;

      portENTER_CRITICAL(&stateMux);
      // Hard Lock Safeguard Boundaries
      if (soilMoisture > moistureMax || waterLevel < 15 || sensorFaultAlert || dhtFaultAlert) {
        valveLocked = true;
        if (valveStatus) {
          writeServoAngle(0); // Rotate servo back to seal fluid path
          valveStatus = false;
        }
      } else if (soilMoisture < (moistureMin + 5.0)) { 
        valveLocked = false; // Release lock with built-in hysteresis buffer margin
      }

      if (!valveLocked) {
        if (soilMoisture < moistureMin && !valveStatus) {
          moistureBeforeWatering = soilMoisture;
          valveStartTime = now;
          wateringAttempts++;
          valveStatus = true;
          writeServoAngle(90); // Rotate servo to 90 degrees to open fluid path
        }
      }
      portEXIT_CRITICAL(&stateMux);

      // ── STEP 3: LOG TO LOCAL STORAGE (DELTA LOG FILTERING) ──────
      if (sdCardPresent) {
        if (abs(soilMoisture - lastLoggedMoisture) >= 1.5 || abs(temperature - lastLoggedTemp) >= 0.8) {
          File file = SD.open("/plantdata.csv", FILE_APPEND);
          if (file) {
            file.printf("%lu,%.1f,%.1f,%.1f,%d\n", now, soilMoisture, temperature, lightLux, waterLevel);
            file.close();
            lastLoggedMoisture = soilMoisture;
            lastLoggedTemp = temperature;
          } else {
            sdCardPresent = false; // Mark bus dropped on structural write failures
          }
        }
      }
    }

    // ── STEP 4: EMERGENCY TIMEOUT OVERRIDE SHUTDOWN ──────────────
    if (valveStatus && (now - valveStartTime >= MAX_VALVE_TIME)) {
      portENTER_CRITICAL(&stateMux);
      writeServoAngle(0); // Safely force close the servo valve gate
      valveStatus = false;
      portEXIT_CRITICAL(&stateMux);

      float moistureChange = soilMoisture - moistureBeforeWatering;
      if (moistureChange < 1.5 && wateringAttempts >= MAX_ATTEMPTS) {
        portENTER_CRITICAL(&stateMux);
        valveLocked = true; // Hard lockout safety to prevent unmonitored fluid flow
        portEXIT_CRITICAL(&stateMux);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(100)); // Relinquish CPU slices to the scheduler
  }
}

// ════════════════════════════════════════════════════════════════
//  CORE 0: ASYNCHRONOUS NETWORK STACK & CLOUD SYNC TASK
// ════════════════════════════════════════════════════════════════
void networkCommunicationTask(void * pvParameters) {
  esp_task_wdt_add(NULL);
  unsigned long lastNetworkSync = 0;
  unsigned long exponentialBackoffDelay = 5000;

  WiFi.begin(ssid, pass);
  Blynk.config(BLYNK_AUTH_TOKEN);

  for (;;) {
    esp_task_wdt_reset();
    unsigned long now = millis();

    if (WiFi.status() != WL_CONNECTED) {
      Blynk.disconnect();
      vTaskDelay(pdMS_TO_TICKS(exponentialBackoffDelay));
      if (WiFi.status() != WL_CONNECTED) {
        WiFi.disconnect();
        WiFi.begin(ssid, pass);
        // Exponential backoff strategy optimization capped at 1 minute
        exponentialBackoffDelay = fmin(exponentialBackoffDelay * 2, 60000);
      }
    } else {
      exponentialBackoffDelay = 5000; // Reset network delay frame tracker
      if (!Blynk.connected()) {
        Blynk.connect(2000); // Handshake timeout window boundary to prevent task locks
      } else {
        Blynk.run();

        // Control data synchronization cadence independently of automation operations
        if (now - lastNetworkSync >= 4000) {
          lastNetworkSync = now;

          portENTER_CRITICAL(&stateMux);
          Blynk.virtualWrite(V0, soilMoisture);
          Blynk.virtualWrite(V1, temperature);
          Blynk.virtualWrite(V2, humidity);
          Blynk.virtualWrite(V3, lightLux);
          Blynk.virtualWrite(V5, valveStatus); // Syncs physical gate position with dashboard status indicator
          Blynk.virtualWrite(V7, waterLevel);
          portEXIT_CRITICAL(&stateMux);

          // Build Diagnostics Status String Safely
          String status = "✅ Nominal Sync Active";
          if (valveLocked) status = "🚨 System Warning Lockout! Check fluid lines or sensors.";
          if (sensorFaultAlert || dhtFaultAlert) status = "⚠️ Diagnostic Warning: Hardware degradation.";
          Blynk.virtualWrite(V11, status);
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

// ════════════════════════════════════════════════════════════════
//  SERVO MECHANICAL ANGULAR TRANSLATION INTERFACE (ESP32 Core v3.x)
// ════════════════════════════════════════════════════════════════
void writeServoAngle(int angle) {
  angle = constrain(angle, 0, 180);
  // Linearly maps input degrees scale directly onto the 16-bit PWM resolution bounds
  uint32_t duty = SERVO_MIN_DUTY + (((SERVO_MAX_DUTY - SERVO_MIN_DUTY) * angle) / 180);
  
  // In ESP32 Core v3.x, ledcWrite accepts the exact GPIO PIN instead of a channel
  ledcWrite(SERVO_PIN, duty);
}

// ════════════════════════════════════════════════════════════════
//  THRESHOLD DEFINITIONS LAYER
// ════════════════════════════════════════════════════════════════
void setThresholds() {
  if (plantType == 1) {
    moistureMin = 10; moistureMax = 30; tempMin = 15; tempMax = 40;
  } else if (plantType == 2) {
    moistureMin = 50; moistureMax = 80; tempMin = 20; tempMax = 35;
  } else if (plantType == 3) {
    moistureMin = 40; moistureMax = 70; tempMin = 18; tempMax = 30;
  } else {
    moistureMin = 30; moistureMax = 60; tempMin = 15; tempMax = 35;
  }
}

// ── INBOUND BLYNK REMOTE DESCENT CALLBACKS ───────────────────────
BLYNK_WRITE(V8) { 
  portENTER_CRITICAL(&stateMux);
  plantType = param.asInt(); 
  setThresholds(); 
  portEXIT_CRITICAL(&stateMux);
}

BLYNK_WRITE(V10) { 
  if (param.asInt() == 1) {
    portENTER_CRITICAL(&stateMux);
    valveLocked = false; 
    wateringAttempts = 0; 
    portEXIT_CRITICAL(&stateMux);
  } 
}

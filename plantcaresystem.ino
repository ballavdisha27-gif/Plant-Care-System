#include <DHT.h>
#include <BH1750.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>
#include <time.h>

#define RELAY_PIN   26
#define BUZZER_PIN  4
#define SOIL_PIN    34
#define DHT_PIN     15
#define SD_CS_PIN   5

#define BLYNK_TEMPLATE_ID   "TMPL34YaDTWgj"
#define BLYNK_TEMPLATE_NAME "Plant Care System"
#define BLYNK_AUTH_TOKEN    AUTH

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include "secrets.h"

char ssid[] = WIFI_SSID;
char pass[] = WIFI_PASS;

// ── Sensor readings ──────────────────────────────────────────────
float soilMoisture = 45.0;
float temperature  = 26.5;
float humidity     = 60.0;
float lightLux     = 300.0;
int   healthScore  = 78;
bool  pumpStatus   = false;
int   waterLevel   = 80;

// ── Control flags ────────────────────────────────────────────────
bool manualMode       = false;
bool waterAlertSent   = false;
bool pumpLocked       = false;
int  wateringAttempts = 0;
bool sensorFaultNotified = false;
unsigned long lastSend      = 0;
unsigned long pumpStartTime = 0;
#define MAX_PUMP_TIME 30000
#define MAX_ATTEMPTS  3
float moistureBeforeWatering = 0;

// ── Plant type & thresholds ──────────────────────────────────────
int   plantType   = 4;
float moistureMin = 30, moistureMax = 60;
float tempMin     = 15, tempMax     = 35;
float humidityMin = 30, humidityMax = 80;
float lightMin    = 1000, lightMax  = 80000;

// ── Alert flags ──────────────────────────────────────────────────
bool lightLowAlert         = false;
bool lightHighAlert        = false;
bool humidityHighAlert     = false;
bool humidityLowAlert      = false;
bool tempHighAlert         = false;
bool tempLowAlert          = false;
bool sensorFaultAlert      = false;
bool dhtFaultAlert         = false;
bool lightSensorFaultAlert = false;
bool waterSensorFaultAlert = false;
bool pumpFaultAlert        = false;

// ── Watering schedule ────────────────────────────────────────────
int  scheduleHour       = 7;
int  scheduleMinute     = 0;
bool scheduledWaterDone = false;
int  lastWateredDay     = -1;

// ── Sensor objects ───────────────────────────────────────────────
#define DHT_TYPE DHT22
DHT     dht(DHT_PIN, DHT_TYPE);
BH1750  lightMeter;

// ════════════════════════════════════════════════════════════════
//  THRESHOLDS
// ════════════════════════════════════════════════════════════════
void setThresholds() {
  if (plantType == 1) {
    moistureMin = 10;  moistureMax = 30;
    tempMin     = 15;  tempMax     = 40;
    humidityMin = 20;  humidityMax = 50;
    lightMin    = 5000; lightMax   = 90000;
  } else if (plantType == 2) {
    moistureMin = 50;  moistureMax = 80;
    tempMin     = 20;  tempMax     = 35;
    humidityMin = 60;  humidityMax = 90;
    lightMin    = 1000; lightMax   = 50000;
  } else if (plantType == 3) {
    moistureMin = 40;  moistureMax = 70;
    tempMin     = 18;  tempMax     = 30;
    humidityMin = 40;  humidityMax = 70;
    lightMin    = 2000; lightMax   = 60000;
  } else {
    moistureMin = 30;  moistureMax = 60;
    tempMin     = 15;  tempMax     = 35;
    humidityMin = 30;  humidityMax = 80;
    lightMin    = 1000; lightMax   = 80000;
  }
}

// ════════════════════════════════════════════════════════════════
//  BLYNK CALLBACKS
// ════════════════════════════════════════════════════════════════
BLYNK_WRITE(V8) {
  plantType = param.asInt();
  setThresholds();
  Serial.print("Plant type changed: ");
  Serial.println(plantType);
}

BLYNK_WRITE(V10) {
  if (param.asInt() == 1) {
    sensorFaultAlert       = false;
    wateringAttempts       = 0;
    pumpLocked             = false;
    dhtFaultAlert          = false;
    lightSensorFaultAlert  = false;
    waterSensorFaultAlert  = false;
    pumpFaultAlert         = false;
    sensorFaultNotified    = false;
    Blynk.virtualWrite(V11, "✅ System reset by user!");
    Serial.println("System manually reset!");
  }
}

// ════════════════════════════════════════════════════════════════
//  SETUP SENSORS
// ════════════════════════════════════════════════════════════════
void setupSensors() {
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);
  digitalWrite(BUZZER_PIN, LOW);

  dht.begin();
  Wire.begin();
  lightMeter.begin();

  if (SD.begin(SD_CS_PIN)) {
    Serial.println("SD Card ready!");
  } else {
    Serial.println("SD Card failed!");
  }
}

// ════════════════════════════════════════════════════════════════
//  READ SENSORS
// ════════════════════════════════════════════════════════════════
void readSensors() {
  int raw = analogRead(SOIL_PIN);
  soilMoisture = map(raw, 4095, 1500, 0, 100);
  soilMoisture = constrain(soilMoisture, 0, 100);

  temperature = dht.readTemperature();
  humidity    = dht.readHumidity();
  lightLux    = lightMeter.readLightLevel();

  int waterRaw = analogRead(35);
  waterLevel = map(waterRaw, 0, 4095, 0, 100);
  waterLevel = constrain(waterLevel, 0, 100);
}

// ════════════════════════════════════════════════════════════════
//  HEALTH SCORE
// ════════════════════════════════════════════════════════════════
void calculateHealthScore() {
  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("Health score skipped - DHT22 fault!");
    return;
  }
  if (lightLux < 0) {
    Serial.println("Health score skipped - BH1750 fault!");
    return;
  }
  if (soilMoisture <= 0 || soilMoisture >= 100) {
    Serial.println("Health score skipped - soil sensor fault!");
    return;
  }

  int score = 0;

  // Moisture (40%)
  if (soilMoisture >= moistureMin && soilMoisture <= moistureMax)
    score += 40;
  else if (soilMoisture < moistureMin)
    score += (int)soilMoisture;
  else
    score += 20;

  // Temperature (25%)
  if (temperature >= tempMin && temperature <= tempMax)
    score += 25;
  else
    score += 10;

  // Humidity (20%)
  if (humidity >= humidityMin && humidity <= humidityMax)
    score += 20;
  else
    score += 10;

  // Light (15%)
  if (lightLux >= lightMin && lightLux <= lightMax)
    score += 15;
  else
    score += 5;

  healthScore = score;
}

// ════════════════════════════════════════════════════════════════
//  AUTO WATERING
// ════════════════════════════════════════════════════════════════
void autoWatering() {
  if (!manualMode) {

    // Overwatered - LOCK pump (FIX 5: use moistureMax)
    if (soilMoisture > moistureMax) {
      pumpLocked = true;
      pumpStatus = false;
      digitalWrite(RELAY_PIN, HIGH);
      Serial.println("Pump LOCKED - soil too wet!");
      return;
    }

    // Unlock when dried enough
    if (pumpLocked && soilMoisture < 40) {
      pumpLocked = false;
      Serial.println("Pump UNLOCKED!");
    }

    // Don't water if locked
    if (pumpLocked) {
      pumpStatus = false;
      digitalWrite(RELAY_PIN, HIGH);
      return;
    }

    // Sensor fault - stop watering
    if (sensorFaultAlert) {
      pumpStatus = false;
      digitalWrite(RELAY_PIN, HIGH);
      Serial.println("Pump stopped - sensor fault!");
      return;
    }

    // Normal auto watering
    if (soilMoisture < moistureMin) {

      // First time pump turns ON
      if (pumpStatus == false) {
        moistureBeforeWatering = soilMoisture;
        pumpStartTime = millis();
        wateringAttempts++;
        Serial.print("Watering attempt: ");
        Serial.println(wateringAttempts);
      }

      pumpStatus = true;
      digitalWrite(RELAY_PIN, LOW);

      // After 30 seconds check moisture change
      if (millis() - pumpStartTime > MAX_PUMP_TIME) {
        pumpStatus = false;
        digitalWrite(RELAY_PIN, HIGH);
        Serial.println("Pump stopped after 30 seconds");

        float moistureChange = soilMoisture - moistureBeforeWatering;
        Serial.print("Moisture change: ");
        Serial.println(moistureChange);

        if (moistureChange < 2.0) {
          Serial.println("WARNING: Moisture not changing!");
          if (wateringAttempts >= MAX_ATTEMPTS) {
            sensorFaultAlert = true;
            Serial.println("SENSOR FAULT DETECTED!");
          }
        } else {
          wateringAttempts = 0;
          sensorFaultAlert = false;
          Serial.println("Moisture changing normally!");
        }
      }
    }
    else if (soilMoisture > moistureMax) {
      pumpStatus = false;
      digitalWrite(RELAY_PIN, HIGH);
      wateringAttempts = 0;
    }
  }
}

// ════════════════════════════════════════════════════════════════
//  LOG TO SD
// ════════════════════════════════════════════════════════════════
void logToSD() {
  File file = SD.open("/plantdata.csv", FILE_APPEND);
  if (file) {
    file.print(millis());      file.print(",");
    file.print(soilMoisture);  file.print(",");
    file.print(temperature);   file.print(",");
    file.print(humidity);      file.print(",");
    file.print(lightLux);      file.print(",");
    file.print(healthScore);   file.print(",");
    file.println(waterLevel);
    file.close();
    Serial.println("Data logged to SD!");
  } else {
    Serial.println("SD write failed!");
  }
}

// ════════════════════════════════════════════════════════════════
//  SYSTEM HEALTH CHECK
// ════════════════════════════════════════════════════════════════
void checkSystemHealth() {
  String status = "✅ System OK";

  if (sensorFaultAlert) {
    status = "⚠️ Soil sensor fault! Check placement.";
    if (!sensorFaultNotified) {
      Blynk.logEvent("sensor_fault", "⚠️ Soil sensor faulty! Check placement and press Reset.");
      sensorFaultNotified = true;
    }
  } else {
    sensorFaultNotified = false;
  }

  if (isnan(temperature) || isnan(humidity)) {
    if (!dhtFaultAlert) {
      Blynk.logEvent("sensor_fault", "⚠️ Temperature sensor faulty! Check DHT22.");
      dhtFaultAlert = true;
    }
    status = "⚠️ Temperature sensor fault! Check DHT22.";
  } else {
    dhtFaultAlert = false;
  }

  if (lightLux < 0) {
    if (!lightSensorFaultAlert) {
      Blynk.logEvent("sensor_fault", "⚠️ Light sensor faulty! Check BH1750.");
      lightSensorFaultAlert = true;
    }
    status = "⚠️ Light sensor fault! Check BH1750.";
  } else {
    lightSensorFaultAlert = false;
  }

  if (waterLevel <= 0) {
    if (!waterSensorFaultAlert) {
      Blynk.logEvent("sensor_fault", "⚠️ Water sensor faulty! Check wiring.");
      waterSensorFaultAlert = true;
      pumpLocked = true;
    }
    status = "⚠️ Water sensor fault! Check wiring.";
  } else {
    waterSensorFaultAlert = false;
  }

  if (waterLevel < 10 && waterLevel > 0) {
    status = "🚨 Water tank empty! Please refill.";
  }

  if (pumpStatus && (millis() - pumpStartTime > MAX_PUMP_TIME + 5000)) {
    if (!pumpFaultAlert) {
      Blynk.logEvent("sensor_fault", "⚠️ Pump may be faulty! Check connection.");
      pumpFaultAlert = true;
      pumpLocked     = true;
      digitalWrite(RELAY_PIN, HIGH);
      pumpStatus = false;
      Blynk.virtualWrite(V5, pumpStatus);
    }
    status = "⚠️ Pump fault! Pump forced OFF. Check connection.";
  } else {
    pumpFaultAlert = false;
  }

  Blynk.virtualWrite(V11, status);
  Serial.println(status);
}

// ════════════════════════════════════════════════════════════════
//  NTP TIME SETUP
// ════════════════════════════════════════════════════════════════
void setupTime() {
  configTime(19800, 0, "pool.ntp.org");
  Serial.print("Waiting for time sync");
  int retry = 0;
  struct tm info;
  while (!getLocalTime(&info) && retry < 20) {
    delay(500);
    Serial.print(".");
    retry++;
  }
  if (retry < 20) {
    Serial.println("\nTime synced!");
  } else {
    Serial.println("\nTime sync FAILED — scheduled watering disabled.");
    Blynk.virtualWrite(V11, "⚠️ Time sync failed! Scheduled watering disabled.");
  }
}

// ════════════════════════════════════════════════════════════════
//  SCHEDULED WATERING
// ════════════════════════════════════════════════════════════════
void checkScheduledWatering() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to get time");
    return;
  }

  // Reset by date not midnight minute
  if (timeinfo.tm_mday != lastWateredDay) {
    scheduledWaterDone = false;
  }

  if (timeinfo.tm_hour  == scheduleHour   &&
      timeinfo.tm_min   == scheduleMinute  &&
      !scheduledWaterDone) {

    if (soilMoisture > moistureMax) {
      Serial.println("Scheduled watering skipped - soil already wet!");
      scheduledWaterDone = true;
      lastWateredDay = timeinfo.tm_mday;
      return;
    }
    if (waterLevel < 10) {
      Serial.println("Scheduled watering skipped - water tank empty!");
      scheduledWaterDone = true;
      lastWateredDay = timeinfo.tm_mday;
      return;
    }
    if (sensorFaultAlert) {
      Serial.println("Scheduled watering skipped - sensor fault!");
      scheduledWaterDone = true;
      lastWateredDay = timeinfo.tm_mday;
      return;
    }
    if (pumpLocked) {
      Serial.println("Scheduled watering skipped - pump locked!");
      scheduledWaterDone = true;
      lastWateredDay = timeinfo.tm_mday;
      return;
    }

    Serial.println("Scheduled watering started!");
    digitalWrite(RELAY_PIN, LOW);
    pumpStatus    = true;
    pumpStartTime = millis();
    Blynk.virtualWrite(V5, pumpStatus);
    scheduledWaterDone = true;
    lastWateredDay     = timeinfo.tm_mday;
  }
}

// ════════════════════════════════════════════════════════════════
//  SEND DATA TO BLYNK
// ════════════════════════════════════════════════════════════════
void sendDataToBlynk() {
  Blynk.virtualWrite(V0, soilMoisture);
  Blynk.virtualWrite(V1, temperature);
  Blynk.virtualWrite(V2, humidity);
  Blynk.virtualWrite(V3, lightLux);
  Blynk.virtualWrite(V4, healthScore);
  Blynk.virtualWrite(V5, pumpStatus);
  Blynk.virtualWrite(V7, waterLevel);

  Serial.println("---------------------");
  Serial.print("Soil: ");     Serial.println(soilMoisture);
  Serial.print("Temp: ");     Serial.println(temperature);
  Serial.print("Humidity: "); Serial.println(humidity);
  Serial.print("Light: ");    Serial.println(lightLux);
  Serial.print("Health: ");   Serial.println(healthScore);
  Serial.print("Water: ");    Serial.println(waterLevel);
  Serial.print("Pump: ");     Serial.println(pumpStatus ? "ON" : "OFF");
}

// ════════════════════════════════════════════════════════════════
//  ALERTS & TIPS
// ════════════════════════════════════════════════════════════════
void checkAlerts() {
  String tip = "🌱 Plant is doing well!";

  if (soilMoisture > moistureMax) {
    tip = "🌊 Soil too wet! Pump locked. Waiting for soil to dry...";
  }

  if (sensorFaultAlert) {
    tip = "⚠️ Soil sensor may be faulty! Please check placement.";
  }

  if (waterLevel < 20 && !waterAlertSent) {
    Blynk.logEvent("water_low", "⚠️ Water tank is low! Please refill.");
    waterAlertSent = true;
  }
  if (waterLevel >= 20) waterAlertSent = false;

  // Light
  if (lightLux < lightMin) {
    if (!lightLowAlert) {
      Blynk.logEvent("light_low", "🌑 Light too low for your plant!");
      lightLowAlert = true;
    }
    tip = "🌑 Move plant to brighter area.";
  } else if (lightLux > lightMax) {
    if (!lightHighAlert) {
      Blynk.logEvent("light_high", "☀️ Too much light! Move plant to shade.");
      lightHighAlert = true;
    }
    tip = "☀️ Too much direct sun. Move to indirect light.";
  } else {
    lightLowAlert  = false;
    lightHighAlert = false;
  }

  // Humidity
  if (humidity > humidityMax) {
    if (!humidityHighAlert) {
      Blynk.logEvent("humidity_high", "💧 Humidity too high for your plant!");
      humidityHighAlert = true;
    }
    tip = "💧 Humidity high. Ensure good air circulation.";
  } else if (humidity < humidityMin) {
    if (!humidityLowAlert) {
      Blynk.logEvent("humidity_low", "🏜️ Humidity too low for your plant!");
      humidityLowAlert = true;
    }
    tip = "🏜️ Air is dry. Mist leaves occasionally.";
  } else {
    humidityHighAlert = false;
    humidityLowAlert  = false;
  }

  // Temperature
  if (temperature > tempMax) {
    if (!tempHighAlert) {
      Blynk.logEvent("temp_high", "🌡️ Temperature too high for your plant!");
      tempHighAlert = true;
    }
    tip = "🌡️ Too warm. Move plant to cooler spot.";
  } else if (temperature < tempMin) {
    if (!tempLowAlert) {
      Blynk.logEvent("temp_low", "❄️ Temperature too low for your plant!");
      tempLowAlert = true;
    }
    tip = "❄️ Too cold. Move away from AC or window.";
  } else {
    tempHighAlert = false;
    tempLowAlert  = false;
  }

  Blynk.virtualWrite(V9, tip);
}

// ════════════════════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  setupSensors();
  setupTime();
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
  Serial.println("System Ready!");
  delay(2000);
  Blynk.virtualWrite(V11, "🌱 System started! Monitoring your plant...");
}

// ════════════════════════════════════════════════════════════════
//  LOOP
// ════════════════════════════════════════════════════════════════
void loop() {
  // WiFi reconnect without blocking
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected! Reconnecting...");
    WiFi.reconnect();
  }

  Blynk.run();

  // Non-blocking pump auto-stop after 10 seconds
  if (pumpStatus && (millis() - pumpStartTime >= 10000)) {
    digitalWrite(RELAY_PIN, HIGH);
    pumpStatus = false;
    Blynk.virtualWrite(V5, pumpStatus);
    Serial.println("Pump auto-stopped after 10s.");
  }

  if (millis() - lastSend > 3000) {
    lastSend = millis();
    readSensors();
    calculateHealthScore();
    autoWatering();
    checkScheduledWatering();
    sendDataToBlynk();
    checkAlerts();
    checkSystemHealth();
    logToSD();
  }
}
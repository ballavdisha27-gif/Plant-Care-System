🌱 ESP32 Smart Plant Care System

Author

Disha Ballav Mondal

Project Overview

The ESP32 Smart Plant Care System is an IoT-based automatic plant monitoring and watering system. It continuously monitors soil moisture, temperature, humidity, light intensity, and water level, then automatically controls a servo-operated water valve to irrigate plants when required. The system also logs sensor data to an SD card and provides real-time monitoring through the Blynk IoT platform.

---

Features

- 🌱 Automatic plant watering using Servo Motor
- 📊 Real-time monitoring of:
  - Soil Moisture
  - Temperature
  - Humidity
  - Light Intensity
  - Water Tank Level
- 📱 Blynk IoT dashboard
- 💾 SD Card data logging
- 🌐 Wi-Fi auto reconnect
- 🔄 Dual-core FreeRTOS multitasking
- 🛡 ESP32 Hardware Watchdog protection
- 📉 Exponential Moving Average (EMA) sensor filtering
- 🚨 Sensor fault detection
- 🔒 Automatic valve safety lock
- ⏱ Automatic timeout protection
- 🌿 Multiple plant profiles
- 📡 Real-time cloud synchronization

---

Hardware Used

- ESP32 DevKit V1
- DHT22 Sensor
- BH1750 Light Sensor
- Capacitive Soil Moisture Sensor
- Water Level Sensor
- Servo Motor
- SD Card Module
- Breadboard
- Jumper Wires

---

Software Used

- Arduino IDE
- Blynk IoT
- ESP32 Arduino Core
- C++

---

Libraries

- DHT
- BH1750
- WiFi
- Blynk
- SD
- SPI
- Wire
- Time
- ESP Task Watchdog

---

GPIO Connections

GPIO| Device
GPIO 26| Servo Motor
GPIO 34| Soil Moisture Sensor
GPIO 35| Water Level Sensor
GPIO 15| DHT22
GPIO 21| SDA (BH1750)
GPIO 22| SCL (BH1750)
GPIO 5| SD Card CS
GPIO 4| Buzzer

---

System Workflow

1. Read all sensors.
2. Filter sensor values using EMA.
3. Check for sensor faults.
4. Compare readings with plant thresholds.
5. Open servo valve if watering is needed.
6. Close valve after timeout or sufficient moisture.
7. Log data to SD card.
8. Upload data to Blynk dashboard.
9. Continue monitoring continuously.

---

Safety Features

- Hardware Watchdog
- Automatic Valve Lock
- Water Level Protection
- Sensor Failure Detection
- Servo Timeout Protection
- Wi-Fi Auto Reconnect
- SD Card Error Detection

---

Project Structure

Plant-Care-System/
│
├── plantcaresystem.ino
├── secrets.h
├── README.md
├── PRD.md
├── SCHEMA.md
├── DATA_FLOW.md
└── DESIGN.md

---

Future Improvements

- Camera monitoring
- AI-based plant disease detection
- Solar power support
- Multiple plant support
- Mobile notifications
- Voice assistant integration

---

License

This project is developed for educational and IoT learning purposes.

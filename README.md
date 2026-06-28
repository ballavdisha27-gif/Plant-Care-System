# ESP32 Smart Plant Care System

## Software Setup
1. Install Arduino IDE
2. Copy secrets_example.h and rename it to secrets.h
3. Fill in your WiFi and Blynk credentials in secrets.h
4. Open plantcaresystem.ino in Arduino IDE
5. Select Board: ESP32 Dev Module
6. Click Upload

## Libraries to Install
Open Arduino IDE → Tools → Manage Libraries → Search and install:
- DHT sensor library by Adafruit
- BH1750 by Christopher Laws
- Blynk by Volodymyr Shymanskyy
- SD by Arduino

## Hardware Pins
| Pin | Component |
|-----|-----------|
| 26  | Relay Module |
| 4   | Buzzer |
| 34  | Soil Moisture Sensor |
| 15  | DHT22 |
| 5   | SD Card CS |
| 35  | Water Level Sensor |
| SDA/SCL | BH1750 Light Sensor |

## Blynk Dashboard Setup
| Virtual Pin | Widget | Name |
|-------------|--------|------|
| V0 | Gauge | Soil Moisture |
| V1 | Gauge | Temperature |
| V2 | Gauge | Humidity |
| V3 | Gauge | Light |
| V4 | Gauge | Health Score |
| V5 | LED | Pump Status |
| V7 | Gauge | Water Level |
| V8 | Segmented Switch | Plant Type |
| V9 | Label | Care Tips |
| V10 | Button | System Reset |
| V11 | Label | System Status |

## Plant Types
| Number | Plant |
|--------|-------|
| 1 | Cactus / Succulent |
| 2 | Tropical Plant |
| 3 | Vegetable / Herb |
| 4 | General / Default |

## Notes
- Blynk Template ID: TMPL34YaDTWgj
- Scheduled watering time: 7:00 AM daily
- Data logged to SD card as /plantdata.csv
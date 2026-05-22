# Smart Wearable Posture Monitoring and Analytics System using ESP32 and IoT

A low-cost wearable posture correction system that monitors body posture in real time using an ESP32, MPU6050, and dual flex sensors. The system classifies posture into four levels — Good, Moderate, Bad, and Very Bad — and gives immediate feedback through buzzer/LED alerts while also sending posture data to Blynk and Google Sheets for live tracking and session analytics.

## Features

- Real-time posture monitoring
- Dual-sensor fusion using MPU6050 + flex sensors
- Four-level posture classification:
  - Good
  - Moderate
  - Bad
  - Very Bad
- Immediate buzzer and LED alerts for poor posture
- Blynk dashboard for live posture visualization
- Google Sheets logging for session history
- EMA filtering to reduce sensor noise
- Calibration step for personalized posture reference
- Low-latency response suitable for continuous use

## Hardware Used

- ESP32-C6
- MPU6050 IMU sensor
- 2 Flex sensors
- LED
- Buzzer
- Breadboard
- Resistors and capacitors
- Connecting wires
- Power supply

## Software / Services Used

- Arduino IDE
- ESP32 Arduino core
- Blynk IoT
- Google Apps Script
- Google Sheets

## How It Works

1. On startup, the system calibrates the user’s good posture reference.
2. The MPU6050 measures tilt angle and the flex sensors measure neck/back bending.
3. The ESP32 processes the sensor values every 30 ms.
4. EMA filtering smooths noisy pitch readings.
5. Posture is classified into one of four levels based on threshold conditions.
6. If posture becomes Bad or Very Bad, the buzzer and LED alert the user.
7. Posture percentages and session status are sent to Blynk.
8. At the end of a session, data is logged to Google Sheets.

## Posture Classification

- Good: 0° to < 8° and flex deviation 0 to < 30
- Moderate: 8° to < 18° and flex deviation 30 to < 60
- Bad: 18° to < 28° and flex deviation 60 to < 100
- Very Bad: ≥ 28° or flex deviation ≥ 100

# FallGuard: Autonomous Wearable Safety System

**FallGuard** is a zero-touch emergency response system consisting of a custom-built ESP32 smartwatch and a companion Android application. It is designed to autonomously detect severe falls and automatically call for help without requiring any user interaction, making it ideal for situations where a user might be incapacitated or unconscious.

## Features

- **100% Autonomous Zero-Touch Response**: If a fall is detected, the watch alerts the phone via Bluetooth Low Energy (BLE). The phone bypasses the lock screen, wakes up from deep sleep, and automatically dials an emergency contact.
- **Custom Hardware**: Powered by an ESP32-C3 Microcontroller, featuring parallel I2C bus wiring for a minimal wrist footprint.
- **Vitals Monitoring**: Uses an optical sensor (MAX30102) to track Heart Rate and estimate SpO2 levels.
- **Advanced Fall Detection Logic**: 
  - *Current Implementation*: A precise threshold heuristic that looks for free-fall (G-force drop) followed immediately by a sharp impact spike.
  - *Future ML Integration*: Included in this repository are trained TinyML models (CNN/GRU) using the SisFall dataset. The ESP32 is equipped to log data and eventually run these true pattern recognition neural networks on-edge (via TensorFlow Lite).
- **Background Android Execution**: The companion app uses Foreground Services, WakeLocks, and Full-Screen Intents to ensure 24/7 reliability even when the phone aggressively kills background apps.
- **Standalone Timepiece**: Features an independent, hardware-level POSIX clock, allowing it to function normally when disconnected.

## Repository Structure

- `Android_Companion_App/` & `Android_Companion_App_V1_Stable/`: Source code for the Android companion application handling BLE communication and emergency calling.
- `ESP32_Wearable_Fall_Detection/` & `ESP32_Wearable_Fall_Detection_V1_Stable/`: Firmware for the ESP32-C3 smartwatch, written in C/C++ using PlatformIO/Arduino.
- `SisFall/`: Dataset used for training the Machine Learning models.
- `train.py`, `dataset_loader.py`, `model.py`, `inference.py`: Python scripts used for preparing the dataset, training the deep learning model (CNN+GRU), and converting it to TensorFlow Lite.
- `fall_detection_model.h5`, `fall_detection_model.tflite`, `fall_model.h`: The compiled neural network models, including the C-byte array header (`fall_model.h`) ready for flashing to the ESP32.

## Getting Started

### Hardware Requirements
- ESP32-C3 Mini Microcontroller
- MPU6050 (or similar) Accelerometer/Gyroscope
- MAX30102 Pulse Oximeter & Heart-Rate Sensor
- OLED Display
- TP4056 Battery charging module with 500mAh LiPo Battery
- Push button (for hardware SOS interrupt)

### Flashing the Firmware
1. Open the `ESP32_Wearable_Fall_Detection_V1_Stable` directory in PlatformIO or the Arduino IDE.
2. Install the necessary libraries (e.g., Adafruit MPU6050, SparkFun MAX3010x).
3. Connect your ESP32 via USB and upload the code.

### Installing the Android App
1. Open the `Android_Companion_App_V1_Stable` project in Android Studio.
2. Build the APK and install it on your Android device.
3. Grant the required permissions (Location for BLE, Phone Calls, Draw Over Other Apps, Ignore Battery Optimizations).

## Machine Learning Pipeline (v2.0 Scope)

While v1 relies on a threshold heuristic, this repository contains the groundwork for v2.0 using Edge AI. 
To retrain the model using the SisFall dataset:
```bash
python train.py
```
This script will load the dataset, train a hybrid CNN-GRU network, save it as an `.h5` file, and convert it to a `.tflite` model. You can then use standard tooling (like `xxd` or `tflite_micro_compiler`) to convert the TFLite model into the C array found in `fall_model.h`.

## License & Privacy

FallGuard is designed as a privacy-first system. It requires no paid subscriptions and does not route your data through third-party cloud servers. All math and fall heuristics run locally on the edge (the watch), and all communication happens locally over BLE to your smartphone.

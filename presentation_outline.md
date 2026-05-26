
# FallGuard Smartwatch - Presentation Outline

Use this slide-by-slide guide to build your presentation. It is structured to tell a compelling story, starting with the problem and ending with an impressive live demo.

---

## Slide 1: Title Slide
*   **Headline:** FallGuard - Autonomous Wearable Safety System
*   **Sub-headline:** Zero-Touch Emergency Response via BLE and Android
*   **Visual:** A nice photo of your smartwatch and the Android app side-by-side.

## Slide 2: The Problem
*   **Key Point:** When a severe fall occurs, the victim is often incapacitated or unconscious, unable to reach their phone to dial for help.
*   **Key Point:** Traditional smartwatches require user interaction (tapping "Yes, call 911") or complex paid subscriptions.
*   **The Goal:** Build an affordable, privacy-first, 100% autonomous system that calls for help *without* the user needing to touch their phone.

## Slide 3: The Solution Overview
*   **The Hardware:** A custom-built ESP32-C3 mini wearable measuring Vitals (Heart Rate, SpO2) and Motion.
*   **The Software:** An Android Companion App that runs invisibly in the background 24/7.
*   **The "Zero-Touch" Action:** When a fall occurs, the watch alerts the phone, the phone wakes itself up, bypasses the lock screen, and automatically dials an emergency contact.

## Slide 4: Hardware & Architecture (Use Block Diagram here)
*   *Insert the `block_diagram.html` screenshot on this slide.*
*   **Talking Points:** 
    *   Powered by an ESP32-C3 Microcontroller.
    *   Uses a Distributed Architecture: The watch handles the sensor polling and math, pushing lightweight CSV data packets over Bluetooth Low Energy (BLE) to save battery.
    *   Runs an independent, hardware-level POSIX clock, allowing it to function as a standalone timepiece even when disconnected from the phone.

## Slide 5: The "Under the Hood" Algorithms
*   **Fall Detection Logic:** Uses a precise "Threshold Heuristic." It looks for a drop in G-force (free-fall) followed immediately by a sharp spike (the impact). 
*   **Vitals Tracking:** Uses an Optical Sensor (MAX30102) with a Beer-Lambert Law algorithm to estimate SpO2, and a sliding-window average to calculate a clean Heart Rate.

## Slide 6: The "Zero-Touch" Android Software
*   **Talking Points:** Mobile operating systems actively try to kill background apps to save battery. We bypassed this using:
    *   **Foreground Services:** Deep integration with Android to keep the BLE connection alive 24/7.
    *   **WakeLocks:** System-level commands that "shock" the phone's CPU awake from deep sleep during an emergency.
    *   **Full-Screen Intents:** Bypassing the Android Lock Screen to force an emergency override display over whatever app the user is currently using.

## Slide 7: Circuitry & Engineering (Use Circuit Diagram here)
*   *Insert the `circuit_diagram.html` screenshot on this slide.*
*   **Talking Points:**
    *   Custom Power Delivery system using a TP4056 module and a 500mAh LiPo battery for safe charging and discharging.
    *   Parallel I2C bus wiring for the sensors to keep the footprint small on the wrist.
    *   Dedicated hardware safety interrupts for the SOS button so the user can summon help instantly, even if the primary motion loop is busy.

## Slide 8: The Live Demo Execution
*This is the most important part of the presentation!*
1.  **Show the watch:** Show the OLED screen ticking independently.
2.  **Lock your phone:** State clearly, "My phone is now locked and asleep in my pocket." Put the phone on the table.
3.  **Trigger the Fall:** Drop the watch safely on the desk, or hit the SOS button.
4.  **The "Wow" Moment:** Take your hands off the table. Point to the phone as the screen wakes up by itself, turns solid red, counts down from 10, and automatically dials a contact. 

## Slide 9: Future Scope (Version 2.0)
*   **Machine Learning Integration:** We have already built the "Data Logging" mode into the firmware. The next step is utilizing "TinyML" (Edge Impulse) to collect accelerometer data and train an AI Neural Network. This will replace the heuristic math with true pattern recognition to eliminate false alarms (like quickly sitting down on a couch vs. actually falling). 
*   **Location Tracking:** Injecting GPS coordinates via SMS before the phone call is placed.

## Slide 10: Conclusion & Q&A
*   Wrap up and ask for questions.

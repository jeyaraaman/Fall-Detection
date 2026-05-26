#include <Wire.h>

#include "heartRate.h" // Also from SparkFun MAX3010x library
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <MAX30105.h> // SparkFun Library works for MAX30102

#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <sys/time.h>
#include <time.h>

// --- MACHINE LEARNING MODE ---
// Uncomment the line below to disable BLE/OLED and enable high-speed Serial Data Logging for Edge Impulse
#define DATA_LOG_MODE
// -----------------------------

/* ========================================================================= */
/*                              PIN CONFIGURATIONS                           */
/* ========================================================================= */
#define I2C_SDA 8
#define I2C_SCL 9

#define BUTTON_SOS 6 // Moved away from GPIO 2 (a known hardware strapping pin)
#define BUTTON_CANCEL                                                          \
  5 // Moved far away from GPIO 2 to prevent breadboard bridging!
#define BUTTON_MENU                                                            \
  7 // Moved to 7 to bypass potential internal chip conflicts on Pin 4

/* ========================================================================= */
/*                          OLED DISPLAY SETTINGS                            */
/* ========================================================================= */
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// 16x16 Bluetooth Symbol
const unsigned char bmp_ble_on[] PROGMEM = {
    0x01, 0x00, 0x01, 0x80, 0x01, 0x40, 0x01, 0x20, 0x11, 0x10, 0x09,
    0x20, 0x05, 0x40, 0x03, 0x80, 0x05, 0x40, 0x09, 0x20, 0x11, 0x10,
    0x01, 0x20, 0x01, 0x40, 0x01, 0x80, 0x01, 0x00, 0x00, 0x00};

// 16x16 Bluetooth Symbol with a Slash through it
const unsigned char bmp_ble_off[] PROGMEM = {
    0x01, 0x01, 0x01, 0x82, 0x01, 0x44, 0x01, 0x28, 0x11, 0x10, 0x09,
    0x20, 0x05, 0xc0, 0x03, 0x80, 0x07, 0x40, 0x0d, 0x20, 0x19, 0x10,
    0x31, 0x20, 0x41, 0x40, 0x81, 0x80, 0x01, 0x00, 0x00, 0x00};

/* ========================================================================= */
/*                              SENSOR OBJECTS                               */
/* ========================================================================= */

MAX30105 particleSensor;

/* ========================================================================= */
/*                              BLE SETTINGS                                 */
/* ========================================================================= */
BLEServer *pServer = NULL;
BLECharacteristic *pAlertCharacteristic = NULL;
BLECharacteristic *pVitalsCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;

// Custom UUIDs (Can be generated online)
#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define ALERT_CHAR_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define VITALS_CHAR_UUID "8d451240-2aa4-4780-87a4-e53b6fa6c9b3"
#define TIME_CHAR_UUID                                                         \
  "f36414a6-7880-4965-8b83-2945d81b8969" // For receiving Time & Date

String currentTimeStr = "Time Not Set"; // Global variable to hold time

/* ========================================================================= */
/*                              SYSTEM STATE                                 */
/* ========================================================================= */
enum SystemState {
  STATE_NORMAL,
  STATE_FALL_DETECTED, // Pending verification (10s window)
  STATE_ALERT_SENT,
  STATE_SOS
};

SystemState currentState = STATE_NORMAL;
unsigned long fallDetectedTime = 0;
const unsigned long ALARM_TIMEOUT_MS = 10000; // 10 seconds to cancel

// Vitals variables
long lastVitalsUpdate = 0;
int beatAvg = 0;
int spo2Value = 0;
float skinTemperature = 0.0;
int stepCount = 0;

/* ========================================================================= */
/*                              BLE CALLBACKS                                */
/* ========================================================================= */

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) {
    deviceConnected = true;
    lastVitalsUpdate =
        0; // Trigger an INSTANT vitals update on the next loop tick
  };

  void onDisconnect(BLEServer *pServer) { deviceConnected = false; }
};

void syncInternalTime(String timeStr) {
  // Format received: "HH:mm:ss dd/MM"
  int hr, min, sec, day, mon;
  if (sscanf(timeStr.c_str(), "%d:%d:%d %d/%d", &hr, &min, &sec, &day, &mon) ==
      5) {
    struct tm t;
    t.tm_year = 2026 - 1900; // Hardcoded year for display stability or omit
    t.tm_mon = mon - 1;
    t.tm_mday = day;
    t.tm_hour = hr;
    t.tm_min = min;
    t.tm_sec = sec;
    t.tm_isdst = -1;

    time_t t_of_day = mktime(&t);
    struct timeval tv = {.tv_sec = t_of_day};
    settimeofday(&tv, NULL);
    Serial.println("Watch: Internal clock synced!");
  }
}

class MyTimeCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    String rxValue = pCharacteristic->getValue();
    if (rxValue.length() > 0) {
      syncInternalTime(rxValue);
    }
  }
};

/* ========================================================================= */

// Fall Detection logic variables (Sensitivity Increased)
const float FALL_IMPACT_THRESHOLD = 1.6; // Lowered to trigger on softer impacts
const float FREE_FALL_THRESHOLD = 0.8;   // Raised to trigger on smaller drops
bool potentialFall = false;

// Vitals variables
const byte RATE_SIZE =
    8; // Increased from 4 to 8 to create a much smoother moving average
byte rates[RATE_SIZE]; // Array of heart rates
byte rateSpot = 0;
byte validBeats = 0; // Track how many beats we've actually collected
long lastBeat = 0;
float beatsPerMinute;
int displayBeatAvg = 0;  // Value locked for 5 seconds
long currentIrValue = 0; // Global IR value so display doesn't break the queue

// Smartwatch: Activity & Power variables
unsigned long lastActivityTime = 0;
unsigned long lastDisplayTime = 0; // Global for instantaneous screen rendering
bool isScreenOn = true;
volatile uint8_t currentScreenPage = 0; // 0: Health, 1: Activity, 2: System
volatile unsigned long lastMenuPress = 0;

// Smartwatch: Step Counter variables
unsigned long lastStepTime = 0;
const float STEP_THRESHOLD =
    1.15; // Lowered to 1.15g to make shake-testing very easy

// Smartwatch: SpO2 & Temperature variables
long currentRedValue = 0;
long lastSpo2Update = 0;
long maxIR = 0, minIR = 999999;
long maxRed = 0, minRed = 999999;

/* ========================================================================= */
/*                              INTERRUPTS                                   */
/* ========================================================================= */
void IRAM_ATTR isr_sos() {
  lastActivityTime = millis();
  currentState = STATE_SOS;
}

void IRAM_ATTR isr_cancel() {
  lastActivityTime = millis();
  // Catch-all: If we are in ANY kind of alarm or emergency state, this button
  // clears it.
  if (currentState != STATE_NORMAL) {
    currentState = STATE_NORMAL;
    potentialFall = false;
    lastDisplayTime = 0; // Refresh instantly
  }
}

/* ========================================================================= */
/*                                SETUP                                      */
/* ========================================================================= */
void setup() {
  Serial.begin(115200);

  Serial.println("--- System Booting ---");

  // Set up custom I2C Pins
  Wire.begin(I2C_SDA, I2C_SCL);

#ifndef DATA_LOG_MODE
  // Initialize display
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("Diagnostic: SSD1306 OLED not found. Skipping..."));
  } else {
    Serial.println("OLED Display Initialized.");
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Initializing...");
  display.display();
#else
  Serial.println("DATA_LOG_MODE ENABLED. BLE and OLED disabled.");
#endif

  // Initialize Pins
  pinMode(BUTTON_SOS, INPUT_PULLUP);
  pinMode(BUTTON_CANCEL, INPUT_PULLUP);
  pinMode(BUTTON_MENU, INPUT_PULLUP);

  // Only connect interrupts to critical emergency functions
  attachInterrupt(digitalPinToInterrupt(BUTTON_SOS), isr_sos, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_CANCEL), isr_cancel, FALLING);

  // Initialize MPU6050 manually over I2C to avoid library issues
  Wire.beginTransmission(0x68);
  Wire.write(0x6B); // PWR_MGMT_1 register
  Wire.write(0);    // Set to zero to wake up the MPU-6050
  if (Wire.endTransmission() == 0) {
    Serial.println("MPU6050 initialized perfectly!");
  } else {
    Serial.println("Failed to find MPU6050 chip over I2C.");
  }

#ifndef DATA_LOG_MODE
  // Initialize MAX30102
  if (!particleSensor.begin(Wire, I2C_SPEED_STANDARD)) {
    Serial.println("MAX30102 was not found. Please check wiring/power.");
  } else {
    // CALIBRATION specifically for Heart Rate on fingertips
    byte ledBrightness = 0x1F; // 0x1F = ~6.4mA. Enough to penetrate skin but
                               // not blind the sensor
    byte sampleAverage =
        4; // 4 averages removes 50Hz lightbulb buzz from the signal
    byte ledMode =
        2; // 2 = Red and IR only (Green is not needed for MAX30102 HR)
    int sampleRate = 400; // 400Hz / 4 average = exactly 100Hz output speed
                          // (Perfect for the algorithm!)
    int pulseWidth = 411; // Max pulse width for deepest light penetration
    int adcRange = 4096;  // 4096 is the standard range for skin reflection

    particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate,
                         pulseWidth, adcRange);
  }

  // BLE Setup
  BLEDevice::init("ESP32_Fall_Detector");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  pAlertCharacteristic = pService->createCharacteristic(
      ALERT_CHAR_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  pAlertCharacteristic->addDescriptor(new BLE2902());
  pAlertCharacteristic->setValue("0"); // 0 = Normal, 1 = Fall, 2 = SOS

  pVitalsCharacteristic = pService->createCharacteristic(
      VITALS_CHAR_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  pVitalsCharacteristic->addDescriptor(new BLE2902());
  pVitalsCharacteristic->setValue("HR: 0");

  BLECharacteristic *pTimeCharacteristic = pService->createCharacteristic(
      TIME_CHAR_UUID, BLECharacteristic::PROPERTY_WRITE);
  pTimeCharacteristic->setCallbacks(new MyTimeCallbacks());

  pService->start();
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x0);
  BLEDevice::startAdvertising();

  Serial.print("BLE Started. MAC: ");
  Serial.println(BLEDevice::getAddress().toString().c_str());
  Serial.println("--- System Ready & Advertising ---");

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("System Ready!");
  display.display();
  delay(1000);
#endif
}

/* ========================================================================= */
/*                                MAIN LOOP                                  */
/* ========================================================================= */
unsigned long lastTasksTime = 0;

void loop() {
#ifdef DATA_LOG_MODE
  // Run motion sensing as fast as possible for Edge Impulse (around 100Hz)
  readMotion();
  delay(10);
#else
  // 1. MUST run as fast as possible continuously with NO delays for accurate
  // Heart Rate peak detection
  updateVitals();

  // 2. Run the motion and Bluetooth tasks every 50 milliseconds
  if (millis() - lastTasksTime > 50) {
    readMotion();
    handleState();
    handleBLEConnection();

    // Software Poll the Menu button instead of using an Interrupt
    if (digitalRead(BUTTON_MENU) == LOW) {
      if (millis() - lastMenuPress > 300) { // 300ms Debounce
        lastActivityTime = millis();
        if (currentState == STATE_NORMAL) {
          currentScreenPage = (currentScreenPage + 1) % 3;
          lastDisplayTime = 0; // Force Instant OLED update
        }
        lastMenuPress = millis();
      }
    }

    lastTasksTime = millis();
  }

  // 3. ONLY update the OLED screen once per second (1000ms)
  if (millis() - lastDisplayTime > 1000) {
    if (millis() - lastActivityTime > 120000 && currentState == STATE_NORMAL) {
      // Turn off OLED display completely to save battery
      if (isScreenOn) {
        display.ssd1306_command(SSD1306_DISPLAYOFF);
        isScreenOn = false;
      }
    } else {
      // Wake up screen
      if (!isScreenOn) {
        display.ssd1306_command(SSD1306_DISPLAYON);
        isScreenOn = true;
      }
      updateDisplay();
    }
    lastDisplayTime = millis();

    // Diagnostic Heartbeat
    if (deviceConnected) {
      Serial.println("Status: Connected to Phone");
    } else {
      Serial.println("Status: Advertising... Waiting for Phone");
    }
  }
#endif
}

/* ========================================================================= */
/*                             HELPER FUNCTIONS                              */
/* ========================================================================= */
void readMotion() {
#ifndef DATA_LOG_MODE
  if (currentState == STATE_FALL_DETECTED || currentState == STATE_ALERT_SENT ||
      currentState == STATE_SOS) {
    return; // Don't check for falls if we are already in an alert state
  }
#endif

  Wire.beginTransmission(0x68);
  Wire.write(0x3B); // Starting with register 0x3B (ACCEL_XOUT_H)
  Wire.endTransmission(false);
  Wire.requestFrom((uint16_t)0x68, (size_t)6, true);

  if (Wire.available() == 6) {
    int16_t AcX = Wire.read() << 8 | Wire.read();
    int16_t AcY = Wire.read() << 8 | Wire.read();
    int16_t AcZ = Wire.read() << 8 | Wire.read();

    // Default range is +/- 2g scale. 1g = 16384 data point
    float ax = AcX / 16384.0;
    float ay = AcY / 16384.0;
    float az = AcZ / 16384.0;

#ifdef DATA_LOG_MODE
    // Print in format expected by Edge Impulse Data Forwarder
    Serial.print(ax, 4);
    Serial.print(",");
    Serial.print(ay, 4);
    Serial.print(",");
    Serial.println(az, 4);
#else
    // Calculate total acceleration magnitude in g's
    float accelMagnitude = sqrt(pow(ax, 2) + pow(ay, 2) + pow(az, 2));

    // Smartwatch Step Counter logic (Debounced)
    if (accelMagnitude > STEP_THRESHOLD && millis() - lastStepTime > 300) {
      stepCount++;
      lastStepTime = millis();
      lastActivityTime = millis(); // Any walking wakes up the screen
    }

    // Simple heuristic threshold fall detection
    // 1. Detect free fall
    if (accelMagnitude < FREE_FALL_THRESHOLD) {
      potentialFall = true;
      lastActivityTime = millis();
    }

    // 2. Detect impact following free fall
    if (potentialFall && accelMagnitude > FALL_IMPACT_THRESHOLD) {
      currentState = STATE_FALL_DETECTED;
      fallDetectedTime = millis();
      potentialFall = false;
    }
#endif
  }
}

void updateVitals() {
  particleSensor.check(); // Check the sensor for new data chunks

  while (particleSensor.available()) {
    currentIrValue =
        particleSensor.getFIFOIR(); // Read from the hardware buffer
    currentRedValue = particleSensor.getFIFORed();

    // Track AC max/min for SpO2 calculation
    if (currentIrValue > maxIR)
      maxIR = currentIrValue;
    if (currentIrValue < minIR && currentIrValue > 10000)
      minIR = currentIrValue;
    if (currentRedValue > maxRed)
      maxRed = currentRedValue;
    if (currentRedValue < minRed && currentRedValue > 10000)
      minRed = currentRedValue;

    if (checkForBeat(currentIrValue) == true) {
      long delta = millis() - lastBeat;
      lastBeat = millis();

      beatsPerMinute = 60 / (delta / 1000.0);

      // SpO2 empirical estimation calculation (Ratio of Ratios)
      float irAC = (maxIR - minIR) * 1.0;
      float redAC = (maxRed - minRed) * 1.0;
      float irDC = (maxIR + minIR) / 2.0;
      float redDC = (maxRed + minRed) / 2.0;

      if (irDC > 0 && redDC > 0 && irAC > 0) {
        float ratio = (redAC / redDC) / (irAC / irDC);
        spo2Value = 104.0 - 17.0 * ratio;
        if (spo2Value > 100)
          spo2Value = 100;
        if (spo2Value < 80)
          spo2Value = 80;
      }

      // Reset min/max for next beat cycle
      maxIR = 0;
      minIR = 999999;
      maxRed = 0;
      minRed = 999999;

      // Apply a strict biological filter to reject noisy spikes
      if (beatsPerMinute < 180 && beatsPerMinute > 40) {
        rates[rateSpot++] = (byte)beatsPerMinute;
        rateSpot %= RATE_SIZE;
        if (validBeats < RATE_SIZE)
          validBeats++;

        // Take average of readings dynamically
        beatAvg = 0;
        for (byte x = 0; x < validBeats; x++)
          beatAvg += rates[x];
        beatAvg /= validBeats;
      }
    }

    particleSensor
        .nextSample(); // We are finished with this sample, move to the next
  }

  // Update Temperature occasionally (every 10s)
  if (millis() - lastSpo2Update > 10000) {
    skinTemperature = particleSensor.readTemperature();
    lastSpo2Update = millis();
  }

  // Update BLE vitals every 5 seconds (5000 ms)
  if (millis() - lastVitalsUpdate > 5000) {
    if (deviceConnected) {
      // Use CSV format to stay under the 20-byte BLE MTU limit
      // Format: hr,spo2,temp,steps
      String csv = String(beatAvg) + "," + String(spo2Value) + "," +
                   String(skinTemperature, 1) + "," + String(stepCount);

      pVitalsCharacteristic->setValue(csv.c_str());
      pVitalsCharacteristic->notify();
    }
    lastVitalsUpdate = millis();
  }
}

void handleState() {
  switch (currentState) {
  case STATE_NORMAL:
    pAlertCharacteristic->setValue("0"); // Normal
    break;

  case STATE_FALL_DETECTED:
    // We are waiting to see if user cancels the alarm
    if (millis() - fallDetectedTime > ALARM_TIMEOUT_MS) {
      currentState = STATE_ALERT_SENT;

      // Notify Smartphone App!
      if (deviceConnected) {
        pAlertCharacteristic->setValue("1"); // 1 = Fall Detected
        pAlertCharacteristic->notify();
      }
    }
    break;

  case STATE_SOS:
    if (deviceConnected) {
      pAlertCharacteristic->setValue("2"); // 2 = SOS
      pAlertCharacteristic->notify();
    }
    currentState = STATE_ALERT_SENT;
    break;

  case STATE_ALERT_SENT:
    // Stay in this state until button cancel is pressed (handled in interrupt)
    break;
  }
}

void updateDisplay() {
  display.clearDisplay();

  if (currentState != STATE_NORMAL) {
    // Crisis Override Dashboard
    display.setCursor(0, 0);
    display.println("! EMERGENCY MODE !");

    switch (currentState) {
    case STATE_FALL_DETECTED:
      display.print("FALL DETECTED! ");
      display.print((ALARM_TIMEOUT_MS - (millis() - fallDetectedTime)) / 1000);
      display.println("s");
      display.println("Press Canc to Abort");
      break;
    case STATE_ALERT_SENT:
      display.println("ALERT SENT!");
      break;
    case STATE_SOS:
      display.println("SOS TRIGGERED!");
      break;
    }
  } else {
    // Normal Smartwatch Flow

    // Draw BLE icon in the top right corner
    if (!deviceConnected)
      display.drawBitmap(112, 0, bmp_ble_off, 16, 16, SSD1306_WHITE);
    else
      display.drawBitmap(112, 0, bmp_ble_on, 16, 16, SSD1306_WHITE);

    display.setCursor(0, 0);
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      char timeBuff[20];
      strftime(timeBuff, sizeof(timeBuff), "%H:%M:%S %d/%m", &timeinfo);
      display.println(timeBuff);
    } else {
      display.println("Syncing Time...");
    }

    // Pagination (Compressed for 128x32 OLED)
    switch (currentScreenPage) {
    case 0: // Health Page
      display.print("HR  : ");
      if (currentIrValue < 50000)
        display.println("--");
      else {
        display.print(beatAvg);
        display.println(" BPM");
      }

      display.print("SpO2: ");
      if (currentIrValue < 50000 || spo2Value <= 0)
        display.println("--");
      else {
        display.print(spo2Value);
        display.println(" %");
      }

      display.print("Temp: ");
      if (skinTemperature < 10.0)
        display.println("--");
      else {
        display.print(skinTemperature, 1);
        display.println(" C");
      }
      break;

    case 1: // Activity Page
      display.println("-- ACTIVITY --");
      display.print("Steps: ");
      display.println(stepCount);
      break;

    case 2: // System Page
      display.println("-- SYSTEM --");
      display.print("BLE : ");
      if (deviceConnected)
        display.println("Connected");
      else
        display.println("Waiting...");
      break;
    }
  }

  display.display();
}

void handleBLEConnection() {
  // Disconnect event handling
  if (!deviceConnected && oldDeviceConnected) {
    delay(500); // Give the BLE stack the chance to get things ready
    pServer->startAdvertising(); // Restart advertising
    Serial.println("Watch: Disconnected. Advertising restarted.");
    oldDeviceConnected = deviceConnected;
  }
  // Connect event handling
  if (deviceConnected && !oldDeviceConnected) {
    Serial.println("Watch: Phone Connected!");
    oldDeviceConnected = deviceConnected;
  }
}

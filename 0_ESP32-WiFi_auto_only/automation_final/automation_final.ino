/*
 * =======================================================================================
 * Project: Watering Automation System built on base of ESP32-WiFi 
 * Copyright (c) 2024 Oleg Shovkovyy
 * Date: 	21.09.2024
 * Version: 1.0
 * Description: his program manages greenhouse water automation based on readings from moisture 
 *				and humidity sensors. Water is supplied to the greenhouse when the moisture 
 *				level drops below a predefined threshold. The water supply lasts for 30 seconds,
 *				after which it stops. The CPU then continuously monitors the moisture sensors, 
 *				and updates regarding the moisture and humidity status are displayed both on the
 *				Serial Monitor and in the Arduino Cloud.
 * =======================================================================================
 */
 
 /*
 * DISCLAIMER:
 * This code is shared for educational and demonstration purposes only.
 * Ensure you replace placeholder values with your own secure credentials before deploying.
 */
 

// Include necessary libraries
#include <ArduinoIoTCloud.h>
#include <Arduino_ConnectionHandler.h>
#include "DHT.h"
#include <time.h>

// Define secret values
#define SECRET_SSID "xxxxxxxxxxxxxxxxxxx"
#define SECRET_OPTIONAL_PASS "xxxxxxxxxxxxxxxxx"
#define SECRET_DEVICE_KEY "xxxxxxxxxx#xx"
#define DEVICE_LOGIN_NAME "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  

// Pin definitions
#define DHTPIN  15
#define DHTTYPE DHT11
#define MOISTURE_PIN_1 34
#define MOISTURE_PIN_2 35
#define RELAY_1 25
#define RELAY_2 33

// Sensor calibration
#define DRY_VALUE_1 2900 
#define WET_VALUE_1 1400

#define DRY_VALUE_2 3050 
#define WET_VALUE_2 2120

// Global variables
int moisture_1 = 0;   // For moisture sensor 1
int moisture_2 = 0;   // For moisture sensor 2
int threshold = 40;   // Default threshold
int humidity = 0;     // Starting value
int temperature = 0;  // Starting value
int ripeness = 0;     // Starting value
bool manual = false;  // the value is trus once changed on Arduino Cloud
DHT dht(DHTPIN, DHTTYPE);

// Timing constants
const unsigned long MANUAL_DURATION = 30000;  // 30 seconds
const unsigned long RELAY_ON_DURATION = 30000;  // 30 seconds
const unsigned long RELAY_COOLDOWN = 20000;  // 20 seconds
const unsigned long LOG_INTERVAL = 5000;  // Limit logging to once every 5 seconds
const unsigned long SENSOR_READ_INTERVAL = 100;  // Interval for reading sensors

// Timezone settings for Bangkok (GMT+7)
const long gmtOffset_sec = 7 * 3600; // 7 hours offset
const int daylightOffset_sec = 0;   // No daylight saving

// Variables
bool relay_1_on = false, relay_2_on = false;
unsigned long relay_1_start_time = 0, relay_2_start_time = 0;
unsigned long relay_1_off_time = 0, relay_2_off_time = 0;  // Store when relays turn off
unsigned long last_reading_time = 0;
unsigned long last_log_time = 0;  // For limiting log frequency
unsigned long last_sensor_read_time = 0; // Timer for sensor readings

// Function declarations
void printSensorData(unsigned long current_time);
void readSensors();
void adjustThreshold();
void manualMode(unsigned long current_time);
void autoMode(unsigned long current_time);
void controlRelay(int moisture, int threshold, int relay_pin, bool &relay_on, unsigned long &relay_start_time, unsigned long &relay_off_time, unsigned long current_time);

// Initialize cloud properties
void initProperties() {
    ArduinoCloud.setBoardId(DEVICE_LOGIN_NAME);
    ArduinoCloud.setSecretDeviceKey(SECRET_DEVICE_KEY);
    ArduinoCloud.addProperty(humidity, READ, ON_CHANGE, NULL);
    ArduinoCloud.addProperty(moisture_1, READ, ON_CHANGE, NULL);
    ArduinoCloud.addProperty(ripeness, READWRITE, ON_CHANGE, onRipenessChange);
    ArduinoCloud.addProperty(temperature, READ, ON_CHANGE, NULL);
    ArduinoCloud.addProperty(manual, READWRITE, ON_CHANGE, onManualChange);
}


// WiFi connection handler
WiFiConnectionHandler ArduinoIoTPreferredConnection(SECRET_SSID, SECRET_OPTIONAL_PASS);

// Setup function
void setup() {
    Serial.begin(115200);
    //delay(1500);

    // Initialize pins
    pinMode(MOISTURE_PIN_1, INPUT);
    pinMode(MOISTURE_PIN_2, INPUT);

    // Ensure relays are OUTPUT on startup
    pinMode(RELAY_1, OUTPUT);
    pinMode(RELAY_2, OUTPUT);

    // Ensure relays are off on startup
    digitalWrite(RELAY_1, LOW);
    digitalWrite(RELAY_2, LOW);

    // Initialize sensors
    dht.begin();
    initProperties();
    ArduinoCloud.begin(ArduinoIoTPreferredConnection);

    // Set the debug level
    setDebugMessageLevel(2);
    ArduinoCloud.printDebugInfo();

    // Set timezone to GMT+7 for Bangkok
    const long gmtOffset_sec = 7 * 3600;  // GMT+7 offset in seconds
    const int daylightOffset_sec = 0;    // No daylight saving in Bangkok

    // Initialize the time with NTP servers
     configTime(gmtOffset_sec, daylightOffset_sec, "pool.ntp.org", "time.nist.gov");

}
void loop() {
    ArduinoCloud.update();  // Update cloud variables
    unsigned long current_time = millis();

    if (manual) {
        manualMode(current_time);  // If in manual mode, handle it
    } else {
        autoMode(current_time);  // Handle automatic mode if not manual
        printSensorData(current_time);  // Only print sensor data in Auto Mode with limited frequency
    }

    // Non-blocking sensor readings
    if (current_time - last_sensor_read_time >= SENSOR_READ_INTERVAL) {
        readSensors();
        last_sensor_read_time = current_time;
    }
}

// Manual Mode: Pumps run for 30 seconds then turn off
void manualMode(unsigned long current_time) {
    static unsigned long last_manual_log_time = 0;  // Tracks the last time a message was printed
    const unsigned long manual_log_interval = 1000; // Log every 1 second

    startRelay(RELAY_1, relay_1_on, relay_1_start_time, current_time);
    startRelay(RELAY_2, relay_2_on, relay_2_start_time, current_time);

    unsigned long elapsed_time = current_time - relay_1_start_time;

    if (elapsed_time >= MANUAL_DURATION) {
        stopRelay(RELAY_1, relay_1_on);
        stopRelay(RELAY_2, relay_2_on);
        Serial.println("\nManual Mode Timeout: Both Relays OFF");
        manual = false;  // Switch to AUTO mode after manual completion
        Serial.println("\nSwitching to AUTO mode");
    } else if (current_time - last_manual_log_time >= manual_log_interval) {
        Serial.println("Manual Mode is ON | Time left: " + String((MANUAL_DURATION - elapsed_time) / 1000) + " seconds");
        last_manual_log_time = current_time;  // Update the last log time
    }
}


// Auto Mode: Control pumps based on moisture sensor readings
void autoMode(unsigned long current_time) {
    adjustThreshold();

    // Control relays
    controlRelay(moisture_1, threshold, RELAY_1, relay_1_on, relay_1_start_time, relay_1_off_time, current_time);
    controlRelay(moisture_2, threshold, RELAY_2, relay_2_on, relay_2_start_time, relay_2_off_time, current_time);
}

// Consolidated relay start function
void startRelay(int relay_pin, bool &relay_on, unsigned long &relay_start_time, unsigned long current_time) {
    if (!relay_on) {
        relay_start_time = current_time;
        relay_on = true;
        digitalWrite(relay_pin, HIGH);
    }
}

// Consolidated relay stop function
void stopRelay(int relay_pin, bool &relay_on) {
    if (relay_on) {
        relay_on = false;
        digitalWrite(relay_pin, LOW);
    }
}

// Control relays based on sensor data and implement cooldown logic
void controlRelay(int moisture, int threshold, int relay_pin, bool &relay_on, unsigned long &relay_start_time, unsigned long &relay_off_time, unsigned long current_time) {
    if (moisture < threshold && !manual) {
        // Cooldown period check
        if (!relay_on && (current_time - relay_off_time >= RELAY_COOLDOWN)) {
            startRelay(relay_pin, relay_on, relay_start_time, current_time);
            // Print relay status using a more descriptive message
            if (relay_pin == RELAY_1) {
                Serial.println("Relay 1: ON (Auto)");
            } else if (relay_pin == RELAY_2) {
                Serial.println("Relay 2: ON (Auto)");
            }
        }
    }

    if (relay_on && (current_time - relay_start_time >= RELAY_ON_DURATION)) {
        stopRelay(relay_pin, relay_on);
        relay_off_time = current_time;  // Store the time when relay turns off
        // Print relay status using a more descriptive message
        if (relay_pin == RELAY_1) {
            Serial.println("Relay 1: OFF (Auto Timer)");
        } else if (relay_pin == RELAY_2) {
            Serial.println("Relay 2: OFF (Auto Timer)");
        }
    }
}


// Optimized sensor reading (reduced to 5 readings)
void readSensors() {
    int moisture_total_1 = 0;
    int moisture_total_2 = 0;
    
    for (int i = 0; i < 5; i++) {  // Reduced to 5 readings for optimization
        moisture_total_1 += analogRead(MOISTURE_PIN_1);
        moisture_total_2 += analogRead(MOISTURE_PIN_2);
        delay(1000); // Delay for 1 second between each reading
    }
    int raw_moisture_1 = moisture_total_1 / 5; 
    int raw_moisture_2 = moisture_total_2 / 5;

    if (raw_moisture_1 > DRY_VALUE_1) { moisture_1 = 0;} 
    else if (raw_moisture_1 < WET_VALUE_1) { moisture_1 = 100; } 
    else { moisture_1 = map(raw_moisture_1, WET_VALUE_1, DRY_VALUE_1, 100, 0); } 
   
    if (raw_moisture_2 > DRY_VALUE_2) { moisture_2 = 0;} 
    else if (raw_moisture_2 < WET_VALUE_2) { moisture_2 = 100;} 
    else { moisture_2 = map(raw_moisture_2, WET_VALUE_2, DRY_VALUE_2, 100, 0);}
   
    // Reading temperature and humidity
    temperature = dht.readTemperature();
    humidity = dht.readHumidity();

    if (isnan(temperature) || isnan(humidity)) {
        Serial.println("Failed to read from DHT sensor!");
    }
}

// Adjust threshold based on ripeness
void adjustThreshold() {
    if (ripeness >= 20 && ripeness < 40) {
        threshold = 30;
    } else if (ripeness >= 40 && ripeness < 60) {
        threshold = 20;
    } else if (ripeness >= 60) {
        threshold = 10;
    } else {
        threshold = 40;  // Default threshold
    }
}

// Print sensor data with limited frequency
void printSensorData(unsigned long current_time) {
    if (current_time - last_log_time >= LOG_INTERVAL) {
        // Get current time
        time_t now;
        struct tm timeinfo;
        if (!getLocalTime(&timeinfo)) {
            Serial.println("Failed to obtain time");
        } else {
     
            now = mktime(&timeinfo) + 7 * 3600;
            gmtime_r(&now, &timeinfo); // Convert back to `struct tm` for printing
            char buffer[26];
            strftime(buffer, sizeof(buffer), "%H:%M:%S", &timeinfo);
            Serial.print("Current Time: ");
            Serial.println(buffer);
        }
        Serial.print("Moisture Sensor 1: ");
        Serial.print(moisture_1);
        Serial.print("% (Raw ");
        Serial.print(analogRead(MOISTURE_PIN_1)); // can also save and print this if needed
        Serial.println(")");

        Serial.print("Moisture Sensor 2: ");
        Serial.print(moisture_2);
        Serial.print("% (Raw ");
        Serial.print(analogRead(MOISTURE_PIN_2)); // can also save and print this if needed
        Serial.println(")");

        Serial.print("Relay 1: ");
        Serial.println(relay_1_on ? "ON" : "OFF");
        Serial.print("Relay 2: ");
        Serial.println(relay_2_on ? "ON" : "OFF");


        // Print ripeness
        Serial.print("Ripeness: ");
        Serial.println(ripeness);

        // Print temperature
        Serial.print("Temperature: ");
        Serial.print(temperature);
        Serial.println(" °C");
        
        // Print humidity
        Serial.print("Humidity: ");
        Serial.print(humidity);
        Serial.println(" %");
        Serial.println();  // Add a blank line for better readability

        last_log_time = current_time;  // Update log time

    }
}

// Handle ripeness change
void onRipenessChange() {
    Serial.println("Ripeness has changed");
    Serial.print("New Ripeness Value: ");
    Serial.println(ripeness);
}

// Handle manual mode change
void onManualChange() {
    Serial.println("Manual mode has changed");
}

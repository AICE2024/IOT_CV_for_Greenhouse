/*
 * ===============================================
 * Project: ESP32-Based Ripeness Data Receiver
 * Copyright (c) 2024 AICE2024
 * Date:    07-12-2024
 * Version: 1.0
 * Description: This program is designed for an ESP32 device to receive ripeness data sent 
 *              via ESP-NOW. It operates as a Wi-Fi Access Point and uses JSON to parse 
 *              and display received ripeness results, including file names and ripeness 
 *              percentages, sent from another ESP32 device.
 * ===============================================
 */

 
 /*
 * DISCLAIMER:
 * This code is shared for educational and demonstration purposes only.
 * Ensure you replace placeholder values with your own secure credentials before deploying.
 */
 

#include <esp_now.h>
#include <WiFi.h>
#include <ArduinoJson.h>

// ===================================== Configuration =====================================
const char* RECEIVER_SSID = "ESP32_RECEIVER";   // Receiver SSID for soft-AP
const char* RECEIVER_PASSWORD = "123456789";   // Receiver password for soft-AP

// ===================================== Callback for Receiving Data =====================================
// Handles data reception, parses JSON, and prints ripeness results.
void OnDataRecv(const esp_now_recv_info_t *esp_now_info, const uint8_t *incomingData, int len) {
    char jsonString[len + 1];
    memcpy(jsonString, incomingData, len);
    jsonString[len] = '\0';
    
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, jsonString);
    
    if (error) {
        Serial.println("JSON parsing failed");
        return;
    }

    Serial.println("\nReceived Ripeness Results:");
    JsonArray array = doc.as<JsonArray>();
    for (JsonObject result : array) {
        const char* file = result["file"];
        float ripeness = result["ripeness"];
        Serial.printf("File: %s, Ripeness: %.2f%%\n", file, ripeness);
    }
}

// ===================================== Setup Function =====================================
// Initializes Wi-Fi in AP mode and sets up ESP-NOW.
void setup() {
    Serial.begin(115200);
    
    // Set device as a Wi-Fi Station and AP
    WiFi.mode(WIFI_AP_STA);
    
    // Configure soft-AP
    WiFi.softAP(RECEIVER_SSID, RECEIVER_PASSWORD);
    Serial.println("Receiver AP Started");
    Serial.print("AP MAC: ");
    Serial.println(WiFi.softAPmacAddress());

    // Initialize ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }
    
    esp_now_register_recv_cb(OnDataRecv); // Register the data reception callback
}

// ===================================== Loop Function =====================================
// Keeps the ESP32 running and ready to receive data.
void loop() {
    delay(10);
}

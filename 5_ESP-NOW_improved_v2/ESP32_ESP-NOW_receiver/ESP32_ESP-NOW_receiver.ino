/*
 * =======================================================================================
 * Project: ESP32-Based Fruit Ripeness Data Receiver with JSON Parsing
 * Author:  AICE2024
 * Date:    23-11-2024
 * Version: 1.0
 * Description: This program sets up an ESP32 device as a Wi-Fi Access Point and ESP-NOW 
 *              receiver. It receives JSON-encoded fruit ripeness data from another ESP32 
 *              device, parses the data, and displays the ripeness values on the serial monitor.
 * =======================================================================================
 */

#include <esp_now.h>
#include <WiFi.h>
#include <ArduinoJson.h>

// ====== Global Variables ======
// Credentials for the receiver Access Point
const char* RECEIVER_SSID = "ESP32_RECEIVER";
const char* RECEIVER_PASSWORD = "123456789";

// ====== Callback Function for Receiving Data ======
void OnDataRecv(const esp_now_recv_info_t *esp_now_info, const uint8_t *incomingData, int len) {
    // Buffer to store the incoming JSON string
    char jsonString[len + 1];
    memcpy(jsonString, incomingData, len);
    jsonString[len] = '\0';
    
    // Parse the JSON string
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, jsonString);
    
    if (error) {
        Serial.println("JSON parsing failed");
        return;
    }

    // Display the parsed data
    Serial.println("\nReceived Ripeness Results:");
    JsonArray array = doc.as<JsonArray>();
    for (JsonObject result : array) {
        const char* file = result["file"];
        float ripeness = result["ripeness"];
        Serial.printf("File: %s, Ripeness: %.2f%%\n", file, ripeness);
    }
}

// ====== Setup Function ======
void setup() {
    Serial.begin(115200);
    
    // Configure ESP32 as a Wi-Fi Station and Access Point
    WiFi.mode(WIFI_AP_STA);
    
    // Initialize the soft Access Point
    WiFi.softAP(RECEIVER_SSID, RECEIVER_PASSWORD);
    Serial.println("Receiver AP Started");
    Serial.print("AP MAC: ");
    Serial.println(WiFi.softAPmacAddress());

    // Initialize ESP-NOW protocol
    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }
    
    // Register the data receiving callback
    esp_now_register_recv_cb(OnDataRecv);
}

// ====== Main Loop ======
void loop() {
    delay(10);
}

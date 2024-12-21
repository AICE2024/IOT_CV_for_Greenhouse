/*
 * =======================================================================================
 * Project: ESP32-Based Tomato Ripeness and Command Receiver System
 * Copyright (c) 2024 AICE2024
 * Date:    06.12.2024
 * Version: 1.0
 * Description: This program implements a receiver system using ESP32 to process ripeness 
 *              data from a remote ESP32 device. It communicates via ESP-NOW and Bluetooth, 
 *              receives JSON messages, parses ripeness results, and logs commands and data.
 * =======================================================================================
 */

#include <esp_now.h>
#include <WiFi.h>
#include <ArduinoJson.h>

// ===================================== Configuration =====================================
// Credentials for the receiver
const char* RECEIVER_SSID = "ESP32_RECEIVER";  
const char* RECEIVER_PASSWORD = "123456789";

// ===================================== Callback Function =====================================
// Handles incoming ESP-NOW data and processes JSON messages
void OnDataRecv(const esp_now_recv_info* info, const uint8_t* incomingData, int len) {
  char jsonString[len + 1];
  memcpy(jsonString, incomingData, len);
  jsonString[len] = '\0';

  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, jsonString);

  if (error) {
    Serial.println("JSON parsing failed");
    return;
  }
  Serial.printf("Received JSON: %s\n", jsonString);

  // Check if it's a command message
  if (doc.containsKey("COMMAND")) {
    String command = doc["COMMAND"].as<String>();
    Serial.printf("Received Command:  %s\n", command.c_str());
    return;  // Exit since it's a command
  }

  JsonArray array = doc.as<JsonArray>();
  for (JsonObject result : array) {
    if (!result.containsKey("file") || !result.containsKey("ripeness")) {
      continue;
    }
    Serial.println("\nReceived Ripeness Results:");
    const char* file = result["file"];
    float ripeness = result["ripeness"];
    Serial.printf("File: %s, Ripeness: %.2f%%\n", file, ripeness);
  }
}

// ===================================== Setup Function =====================================
// Initializes the receiver's Wi-Fi and ESP-NOW configuration
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

  // Register the callback for receiving data
  esp_now_register_recv_cb(OnDataRecv);
}

// ===================================== Main Loop =====================================
// Keeps the system alive and handles any background tasks
void loop() {
  delay(10);
}

/*
 * =======================================================================================
 * Project: ESP32 Bluetooth-Based Tomato Ripeness and Drone Integration System
 * Copyright (c) 2024 AICE2024
 * Date:    06.12.2024
 * Version: 1.0
 * Description: This program implements a system for detecting tomato ripeness using an ESP32. 
 *              It manages Bluetooth communication to send ripeness data, processes images 
 *              captured by an ESP32-CAM, interacts with a cloud-based inference system for 
 *              ripeness analysis, and controls a Tello drone to capture additional images.
 * =======================================================================================
 */

 /*
 * DISCLAIMER:
 * This code is shared for educational and demonstration purposes only.
 * Ensure you replace placeholder values with your own secure credentials before deploying.
 */
 

#include <Arduino.h>
#include <SD_MMC.h>
#include "camFunctions.h"
#include "InferenceHandler.h"
#include <esp_now.h>
#include <WiFi.h>
#include <vector>
#include <ArduinoJson.h>

// ===================================== Configuration =====================================
// Local network credentials
const char *WIFI_SSID = "xxxxxxxxxxx";
const char *WIFI_PASSWORD = "xxxxxxxxxxxxx";

// GCP server details
const char *host = "xxxxxxxxxxxxxxxxxxxxxxxxxxx"; // Server host
const int httpsPort = 443;                                 // Server port

// Confidence and overlap parameters
const float CONFIDENCE_THRESHOLD = 45.0; // 45%
const float OVERLAP_THRESHOLD = 25.0;    // 25%

// Global variables for timing
unsigned long lastRunTime;
const unsigned long RUN_INTERVAL = 60000; // Run every 1 minute

// ===================================== Inference Configuration =====================================
// Instance of the InferenceHandler library
InferenceHandler inferenceHandler(WIFI_SSID, WIFI_PASSWORD, host, httpsPort);

// Vector storing the ripeness results
std::vector<std::pair<String, float>> ripenessResults; // Store <filename, ripeness%> pairs

// Receiver SSIDs
const char *RECEIVER_SSID = "ESP32_RECEIVER";
const char *ACTION_RECEIVER_SSID = "TELLO_ESP32_CAM";

// Single MAC address array for current peer
uint8_t peerMacAddress[6];

// Define the ripeness threshold
const float RIPENESS_THRESHOLD = 40.0; // Set your desired threshold

// ===================================== Function Prototypes =====================================
String getNextFilePath(const String &folderName, const String &prefix, const String &extension);
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
bool findReceiverMac(const char *ssid, uint8_t *macAddress);
bool initEspNowPeer(const char* ssid);

// ===================================== Setup Function =====================================
// Initializes peripherals and configurations.
void setup() {
    Serial.begin(115200);
    // Initialize SD Card
    if (!SD_MMC.begin("/sdcard", true)) {
        Serial.println("SD Card Mount Failed, Operations halted");
        while (1);
    }
    Serial.println("SD Card mounted successfully");
    lastRunTime = millis() - RUN_INTERVAL;
}

// ===================================== Main Loop =====================================
// Executes the primary logic of capturing images, processing them, and sending results.
void loop() {
    unsigned long currentTime = millis();

    if (currentTime - lastRunTime >= RUN_INTERVAL) {
        Serial.println("Starting operation...");

        // Initialize Camera and capture image section
        if (!initCamera()) {
            Serial.println("Camera initialization failed");
        } else {
            Serial.println("Camera initialized successfully");

            // Capture new image and save to SD card
            String imagePath = getNextFilePath("/camImages", "camImg_", ".jpg");
            if (captureAndSaveImage(imagePath)) {
                Serial.println("Image captured and saved to: " + imagePath);

                deinitCamera();

                // Process image
                if (!inferenceHandler.begin()) {
                    Serial.println("InferenceHandler initialization failed!");
                    return;
                }
                Serial.println("InferenceHandler initialized.");

                InferenceResult imageResult;
                if (inferenceHandler.requestInference(imagePath.c_str(), CONFIDENCE_THRESHOLD, OVERLAP_THRESHOLD, imageResult)) {
                    // Print results
                    Serial.println("Camera Image Analysis:");
                    Serial.printf("Total Objects: %d\n", imageResult.totalObjects);
                    Serial.printf("Ripe Tomatoes: %d\n", imageResult.ripeCount);
                    Serial.printf("Unripe Tomatoes: %d\n", imageResult.unripeCount);
                    Serial.printf("Green Tomatoes: %d\n", imageResult.greenCount);
                    Serial.printf("Ripeness Percentage: %.2f%%\n", imageResult.ripenessPercentage);

                    ripenessResults.push_back({imagePath, imageResult.ripenessPercentage});
                }
                inferenceHandler.end();
            }
        }

        // Send TELLO Start command if latest result exceeds threshold
        if (!ripenessResults.empty() && ripenessResults.back().second > RIPENESS_THRESHOLD) {
            if (initEspNowPeer(ACTION_RECEIVER_SSID)) {
                StaticJsonDocument<64> doc;
                doc["COMMAND"] = "Start_Operation";
                String jsonString;
                serializeJson(doc, jsonString);
                Serial.printf("Sending command: %s\n", jsonString.c_str());
                esp_now_send(peerMacAddress, (uint8_t *)jsonString.c_str(), jsonString.length());
            }

            // Create JSON array from results
            StaticJsonDocument<1024> doc;
            JsonArray array = doc.to<JsonArray>();

            // Add valid results
            for (const auto &result : ripenessResults) {
                if (!result.first.isEmpty() && result.second >= 0.0) {
                    JsonObject obj = array.createNestedObject();
                    obj["file"] = result.first;
                    obj["ripeness"] = result.second;
                } else {
                    Serial.printf("Skipping invalid result: file='%s', ripeness=%.2f\n",
                                  result.first.c_str(), result.second);
                }
            }

            // Send if we have valid results
            if (array.size() > 0) {
                if (initEspNowPeer(RECEIVER_SSID)) {
                    String jsonString;
                    serializeJson(doc, jsonString);
                    if (esp_now_send(peerMacAddress, (uint8_t *)jsonString.c_str(), jsonString.length()) == ESP_OK) {
                        Serial.println("Results sent successfully");
                    } else {
                        Serial.println("Error sending results");
                    }
                }
                ripenessResults.clear();
            } else {
                Serial.println("No valid results to send");
            }

        } else {
            Serial.println("Ripeness percentage below threshold, no action taken");
        }

        Serial.println("Operation Complete, Waiting for next runtime...\n\n");
        lastRunTime = currentTime;
    }
}

// ===================================== Helper Functions =====================================
// Retrieves next file path with an auto-incrementing number.
String getNextFilePath(const String &folderName, const String &prefix, const String &extension) {
    if (!SD_MMC.exists(folderName)) {
        SD_MMC.mkdir(folderName);
        return folderName + "/" + prefix + "1" + extension;
    }

    File root = SD_MMC.open(folderName);
    if (!root || !root.isDirectory()) {
        return folderName + "/" + prefix + "1" + extension;
    }

    int maxNumber = 0;
    File file = root.openNextFile();

    while (file) {
        String fileName = String(file.name());
        if (fileName.startsWith(prefix) && fileName.endsWith(extension)) {
            int startIdx = prefix.length();
            int endIdx = fileName.indexOf(extension);
            String numStr = fileName.substring(startIdx, endIdx);
            int currentNum = numStr.toInt();
            maxNumber = max(maxNumber, currentNum);
        }
        file = root.openNextFile();
    }

    return folderName + "/" + prefix + String(maxNumber + 1) + extension;
}

// Callback for ESP-NOW data send status.
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (status != ESP_NOW_SEND_SUCCESS) {
        Serial.println("Last Packet Send Failed!");
    }
}

// Finds the MAC address of the receiver using its SSID.
bool findReceiverMac(const char *ssid, uint8_t *macAddress) {
    Serial.printf("Scanning for receiver with SSID: %s\n", ssid);

    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    delay(100);

    int n = WiFi.scanNetworks();
    if (n == 0) {
        Serial.println("No networks found");
        return false;
    }

    for (int i = 0; i < n; ++i) {
        if (String(ssid).equals(WiFi.SSID(i))) {
            String bssidStr = WiFi.BSSIDstr(i);
            Serial.printf("Receiver found! SSID: %s, MAC: %s\n", ssid, bssidStr.c_str());

            // Convert BSSID string to bytes
            int values[6];
            if (sscanf(bssidStr.c_str(), "%x:%x:%x:%x:%x:%x",
                       &values[0], &values[1], &values[2],
                       &values[3], &values[4], &values[5]) == 6) {
                for (int j = 0; j < 6; j++) {
                    macAddress[j] = (uint8_t)values[j];
                }
                return true;
            } else {
                Serial.println("Failed to parse MAC address");
                return false;
            }
        }
    }

    Serial.printf("Receiver with SSID '%s' not found\n", ssid);
    return false;
}

// Initializes ESP-NOW peer communication with a specified SSID.
bool initEspNowPeer(const char* ssid) {
    if (!findReceiverMac(ssid, peerMacAddress)) {
        Serial.printf("Peer %s not found!\n", ssid);
        return false;
    }

    // Initialize ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return false;
    }
    esp_now_register_send_cb(OnDataSent);

    // Remove any existing peer (ignoring error if no peer exists)
    esp_now_del_peer(peerMacAddress);

    // Add new peer
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, peerMacAddress, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    peerInfo.ifidx = WIFI_IF_STA;

    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("Failed to add peer");
        return false;
    }

    return true;
}

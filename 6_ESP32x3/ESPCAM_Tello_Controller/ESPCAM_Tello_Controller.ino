/*
 * =======================================================================================
 * Project: ESP32 Drone-Assisted Tomato Ripeness Detection and Command System
 * Author:  AICE2024
 * Date:    06.12.2024
 * Version: 1.0
 * Description: This program implements a system using an ESP32 and a Tello drone to detect 
 *              tomato ripeness. It captures images, analyzes ripeness using cloud-based 
 *              inference (via GCP), and communicates results via ESP-NOW. The system also 
 *              includes a command receiver to initiate operations.
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
#include "TelloESP32.h"
#include <esp_now.h>
#include <vector>

// ===================================== Configuration =====================================
// Tello drone credentials
const char *TELLO_SSID = "TELLO-xxxxx";
const char *TELLO_PASSWORD = "";

// Local network credentials
const char *WIFI_SSID = "xxxxxxxxxxxxxxx";
const char *WIFI_PASSWORD = "xxxxxxxxxxxxxxxx";

// GCP server details
const char *host = "xxxxxxxxxxxxxxxxxxxx;
const int httpsPort = 443;

// ESP-NOW configuration
const char *RECEIVER_SSID = "ESP32_RECEIVER";
const char *DEVICE_SSID = "TELLO_ESP32_CAM";
const char *DEVICE_PASSWORD = "12345678";
uint8_t receiverMacAddress[6];

// Confidence and overlap parameters for inference
const float CONFIDENCE_THRESHOLD = 45.0;
const float OVERLAP_THRESHOLD = 25.0;

// ===================================== Instances =====================================
// Inference handler for processing images
InferenceHandler inferenceHandler(WIFI_SSID, WIFI_PASSWORD, host, httpsPort);

// Tello drone controller
TelloESP32 tello;

// ===================================== Operation State =====================================
// System operation status
bool operationStarted = false;
int currentFlightNumber = 0;
int currentImageNumber = 0;
std::vector<String> capturedImagePaths;

// ===================================== Function Prototypes =====================================
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
bool findReceiverMac();
void initEspNow();
int getNextFlightNumber();
void OnDataRecv(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len);
void startCommandRecMode();

// Callback for image capture
void onImageCaptured(camera_fb_t *fb) {
  if (!fb) return;

  String imagePath = "/flightImages/flight" + String(currentFlightNumber) + "/img_" + String(currentImageNumber++) + ".jpg";

  File file = SD_MMC.open(imagePath.c_str(), FILE_WRITE);
  if (file) {
    file.write(fb->buf, fb->len);
    file.close();
    capturedImagePaths.push_back(imagePath);
    Serial.println("Captured image: " + imagePath);
  }
}

// ===================================== Setup Function =====================================
// Initializes the system, including SD card and command reception mode
void setup() {
  Serial.begin(115200);
  if (!SD_MMC.begin("/sdcard", true)) {
    Serial.println("SD Card Mount Failed, Operation aborted");
    while (1)
      ;
  }
  startCommandRecMode();
}

// ===================================== Main Loop =====================================
// Handles the main operation when a command to start is received
void loop() {
  if (!operationStarted) {
    delay(100);
    return;
  }

  // Initialize camera
  if (!initCamera()) {
    Serial.println("Camera initialization failed, aborting operation");
    while(1);
  }

  // Connect to Tello and perform flight
  if (tello.connect(TELLO_SSID, TELLO_PASSWORD, 10000)) {
    currentFlightNumber = getNextFlightNumber();
    String flightPath = "/flightImages/flight" + String(currentFlightNumber);
    SD_MMC.mkdir(flightPath);

    currentImageNumber = 0;
    startContinuousCapture(1000, onImageCaptured);

    tello.takeoff();
    delay(5000);
    tello.land();

    stopContinuousCapture();
    tello.disconnect();
  } else {
    Serial.println("Failed to connect to Tello drone!");
    startCommandRecMode();
    return;
  }

  deinitCamera();

  // Process captured images and send results
  if (!capturedImagePaths.empty()) {
    if (inferenceHandler.begin()) {
      float totalRipeness = 0.0;
      int validResults = 0;

      // Process images and calculate average ripeness
      for (const String &imagePath : capturedImagePaths) {
        InferenceResult result;
        if (inferenceHandler.requestInference(imagePath.c_str(), CONFIDENCE_THRESHOLD, OVERLAP_THRESHOLD, result)) {
          totalRipeness += result.ripenessPercentage;
          validResults++;
          Serial.printf("Image %s ripeness: %.2f%%\n", imagePath.c_str(), result.ripenessPercentage);
        }
      }

      inferenceHandler.end();

      // Calculate average ripeness
      float averageRipeness = validResults > 0 ? totalRipeness / validResults : 0.0;

      // Send results via ESP-NOW
      initEspNow();

      // Create results JSON
      StaticJsonDocument<200> doc;
      doc["flight_number"] = currentFlightNumber;
      doc["average_ripeness"] = averageRipeness;
      doc["images_processed"] = validResults;

      String jsonString;
      serializeJson(doc, jsonString);

      // Send results
      esp_err_t result = esp_now_send(receiverMacAddress,
                                      (uint8_t *)jsonString.c_str(),
                                      jsonString.length());

      if (result == ESP_OK) {
        Serial.println("Results sent successfully");
      } else {
        Serial.println("Error sending results");
      }

      capturedImagePaths.clear();
    }
  }

  WiFi.disconnect();
  startCommandRecMode();
}

// ===================================== Helper Functions =====================================
// Helper function to initialize ESP-NOW command receive mode
void startCommandRecMode() {
  operationStarted = false;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(DEVICE_SSID, DEVICE_PASSWORD);
  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_register_recv_cb(OnDataRecv);
  Serial.println("ESP-NOW initialized: Waiting for Start command from Base ESP32-Cam..");
}

// Helper function to get next file path with auto-incrementing number
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

// Helper function to initialize ESP-NOW for data transmission
void initEspNow() {
  // Disconnect from any existing WiFi
  WiFi.disconnect();

  // Set WiFi mode to Station
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  // Find receiver's MAC address
  if (!findReceiverMac()) {
    Serial.println("Receiver not found!");
    return;
  }

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Register callback
  esp_now_register_send_cb(OnDataSent);

  // Add peer with proper configuration
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, receiverMacAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  peerInfo.ifidx = WIFI_IF_STA;  // Changed from ESP_IF_WIFI_STA

  // Add peer with error checking
  esp_err_t addStatus = esp_now_add_peer(&peerInfo);
  if (addStatus != ESP_OK) {
    Serial.print("Failed to add peer, error: ");
    Serial.println(addStatus);
    return;
  }

  Serial.println("ESP-NOW initialized successfully");
}


bool findReceiverMac() {
  Serial.println("Scanning for receiver...");

  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  delay(100);

  int n = WiFi.scanNetworks();
  if (n == 0) {
    Serial.println("No networks found");
    return false;
  }

  for (int i = 0; i < n; ++i) {
    if (String(RECEIVER_SSID).equals(WiFi.SSID(i))) {
      String bssid = WiFi.BSSIDstr(i);
      Serial.print("Receiver found! MAC: ");
      Serial.println(bssid);

      // Convert BSSID string to bytes
      int values[6];
      if (sscanf(bssid.c_str(), "%x:%x:%x:%x:%x:%x",
                 &values[0], &values[1], &values[2],
                 &values[3], &values[4], &values[5])
          == 6) {
        for (int j = 0; j < 6; j++) {
          receiverMacAddress[j] = (uint8_t)values[j];
        }
        return true;
      }
    }
  }
  return false;
}

void OnDataRecv(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len) {
  char *jsonData = (char *)malloc(data_len + 1);
  memcpy(jsonData, data, data_len);
  jsonData[data_len] = '\0';

  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, jsonData);
  free(jsonData);

  if (error) {
    Serial.println("Failed to parse JSON");
    return;
  }

  const char *command = doc["COMMAND"];
  if (strcmp(command, "Start_Operation") == 0) {  // Check if command is Start_Operation
    operationStarted = true;
    Serial.println("Received start operation command!");
  }
}

// Helper function to get next flight number
int getNextFlightNumber() {
  String basePath = "/flightImages";
  if (!SD_MMC.exists(basePath)) {
    SD_MMC.mkdir(basePath);
    return 0;
  }

  File root = SD_MMC.open(basePath);
  if (!root || !root.isDirectory()) {
    return 0;
  }

  int maxNumber = -1;
  File file = root.openNextFile();

  while (file) {
    String fileName = String(file.name());
    if (fileName.startsWith("flight")) {
      // Extract number from "flightX" folder name
      int num = fileName.substring(6).toInt();
      maxNumber = max(maxNumber, num);
    }
    file = root.openNextFile();
  }

  return maxNumber + 1;
}
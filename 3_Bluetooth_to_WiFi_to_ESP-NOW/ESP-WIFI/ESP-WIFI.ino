/*
 * ===============================================
 * Project: ESP32-Based Fruit Ripeness Detection with Drone Integration
 * Copyright (c) 2024 Oleg Shovkovyy
 * Date:    19-10-2024
 * Version: 1.0
 * Description: This program combines ESP32, ESP-CAM, and a Tello drone to detect fruit ripeness. 
 *              The ESP-CAM captures images, processes ripeness data, and communicates with the ESP32. 
 *              The ESP32 manages WiFi connections, communicates with the Tello drone for image acquisition, 
 *              processes data from multiple sources, and transmits the total ripeness using ESP-NOW.
 * ===============================================
 */

 /*
 * DISCLAIMER:
 * This code is shared for educational and demonstration purposes only.
 * Ensure you replace placeholder values with your own secure credentials before deploying.
 */
 

#include "common.h"
#include "Base_64.h"
#include "functions.h"

// WiFi credentials for ESP32
const char *ssid_wifi = "xxxxxxxxxxxxxxxxxxx";
const char *pass_wifi = "xxxxxxxxxxxxxxxxxxx";

// WiFi credentials for Tello drone
const char *ssid_tello = "TELLO-xxxxxxxxxxxxx";
const char *pass_tello = "";

// Tello drone communication details
WiFiUDP udp;
const char *TELLO_IP = "192.168.10.1";
const int TELLO_PORT = 8889;
const int LOCAL_PORT = 9000;

// Variables to store ripeness data
float ripeness_cam = 0;
float ripeness_tello = 0;
float total_ripeness = 0;

// ====== Setup Function ======
void setup() {
  Serial.begin(9600);

  // Step 1: Initialize camera, ESP-NOW, and SD card
  initCamera();
  delay(100);
  init_EspNow();
  init_SD();
  delay(100);
  clearSDCardContent();

  // Step 2: Connect to local WiFi network
  // If connection fails within 20 seconds, the ESP32 goes to deep sleep
  if (!wifi_connect_ap(ssid_wifi, pass_wifi)) {
    goToSleep();
  }

  // Step 3: Capture image and calculate ripeness using ESP-CAM
  captureAndSaveImage();
  ripeness_cam = getRipenessFromImage("/esp-cam.jpg");
  clearSDCardContent();

  if (ripeness_cam >= 30) {
    Serial.println("The ripeness is greater than 30%.");

    // Step 4: Disconnect from WiFi and wait 20 seconds before connecting to Tello
    wifi_disconnect(ssid_wifi);
    delay(20 * 1000);

    // Attempt to connect to Tello
    if (!wifi_connect_ap(ssid_tello, pass_tello)) {
      Serial.println("Cannot connect to Tello. Sending ripeness_cam only.");

      // Step 9: Send only ripeness_cam data
      total_ripeness = ripeness_cam;
      if (!wifi_connect_ap(ssid_wifi, pass_wifi)) {
        goToSleep();
      }
      Sent_Data.total_ripeness = total_ripeness;

      // Send total ripeness via ESP-NOW
      SendPred();
      goToSleep();
    } else {
      // Step 5: Communicate with Tello and process images
      udp.begin(TELLO_PORT);

      // Capture and save image using Tello
      captureAndSaveImage();
      Serial.println("Calculating ripeness from Tello...");

      // Disconnect from Tello and reconnect to WiFi
      wifi_disconnect(ssid_tello);
      if (!wifi_connect_ap(ssid_wifi, pass_wifi)) {
        goToSleep();
      }

      // Step 6: Process ripeness data from Tello and calculate total ripeness
      ripeness_tello = processTelloImages();
      total_ripeness = 0.2 * ripeness_cam + 0.8 * ripeness_tello;
      Sent_Data.total_ripeness = total_ripeness;

      // Step 7: Send total ripeness via ESP-NOW
      SendPred();
      goToSleep();
    }
  } else {
    Serial.println("The ripeness is less than 30%.");
    // Step 11: Go to deep sleep for 5 minutes
    goToSleep();
  }
}

// ====== Drone Command Function ======
// Sends a command to the Tello drone
void sendCommand(char *cmd) {
  udp.beginPacket(TELLO_IP, TELLO_PORT);
  udp.write((uint8_t *)cmd, strlen(cmd));
  udp.endPacket();
  Serial.printf("Sent command: %s\n", cmd);
}

// ====== Drone Control Function ======
// Controls Tello drone for basic takeoff and landing
void control_drone_TELLO() {
  sendCommand("command");
  delay(1000);
  sendCommand("takeoff");
  delay(5000);  // Stay in the air for 5 seconds
  sendCommand("land");
}

// ====== Loop Function ======
// The main loop remains empty as all operations are executed once during startup
void loop() {
  // Nothing to do here
}

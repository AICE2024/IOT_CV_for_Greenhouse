/*
 * =======================================================================================
 * Project: ESP32-Based Image Capture and Ripeness Analysis with Drone Integration
 * Copyright (c) 2024 AICE2024
 * Date:    13-11-2024
 * Version: 1.0
 * Description: This program implements a fruit ripeness detection system using an ESP32 camera 
 *              and Tello drone. The ESP32 captures an image, calculates ripeness, and communicates 
 *              the results using ESP-NOW. If the ripeness exceeds a threshold, the ESP32 connects 
 *              to the Tello drone to capture additional images, processes data from multiple sources, 
 *              and transmits the final ripeness value for further use.
 * =======================================================================================
 */


  /*
 * DISCLAIMER:
 * This code is shared for educational and demonstration purposes only.
 * Ensure you replace placeholder values with your own secure credentials before deploying.
 */
 

#include "common.h"
#include "functions.h"

// WiFi credentials for local network
const char *ssid_wifi = "xxxxxxxxxxxxxxxxx";
const char *pass_wifi = "xxxxxxxxxxxxxxxxxxx";

// WiFi credentials for Tello drone
const char *ssid_tello = "TELLO-xxxxxxxxxxxxxxxx";
const char *pass_tello = "";

// Tello drone communication details
WiFiUDP udp;
const char *TELLO_IP = "192.168.10.1";
const int TELLO_PORT = 8889;
const int LOCAL_PORT = 9000;

// Variables for ripeness data
float ripeness_cam = 0;
float ripeness_tello = 0;
float total_ripeness = 0;

// ======================================= Setup =======================================
void setup() {
  Serial.begin(9600);

  // Step 1: Initialize camera, ESP-NOW, and SD card
  initCamera();
  delay(100);
  init_EspNow();
  init_SD();
  delay(100);
  clearSDCardContent();

  // Step 2: Connect to local network
  // If the connection fails within 20 seconds, the ESP32 will go to deep sleep
  if (!wifi_connect_ap(ssid_wifi, pass_wifi)) {
    goToSleep();
  }

  // Step 3: Process image and calculate ripeness using ESP-CAM
  ripeness_cam = processCamImage();
  if (ripeness_cam >= 30) {
    Serial.println("The ripeness is greater than 30%.");

    // Step 4: Disconnect from local WiFi and wait for 20 seconds
    wifi_disconnect(ssid_wifi);
    delay(20 * 1000);

    // Attempt to connect to Tello drone
    if (!wifi_connect_ap(ssid_tello, pass_tello)) {
      Serial.println("Cannot connect to Tello. Sending ripeness_cam only.");

      // Step 9: Send only ESP-CAM ripeness data
      total_ripeness = ripeness_cam;
      if (!wifi_connect_ap(ssid_wifi, pass_wifi)) {
        goToSleep();
      }
      Sent_Data.total_ripeness = total_ripeness;

      // Send ripeness data via ESP-NOW
      SendPred();
      goToSleep();
    } else {
      // Step 5: Begin communication with Tello drone
      udp.begin(TELLO_PORT);

      // Add logic to control Tello and process images
      Serial.println("Calculating ripeness from Tello...");
      wifi_disconnect(ssid_tello);

      // Reconnect to local WiFi
      if (!wifi_connect_ap(ssid_wifi, pass_wifi)) {
        goToSleep();
      }

      // Step 6: Process Tello images and calculate total ripeness
      ripeness_tello = processTelloImages();
      total_ripeness = 0.2 * ripeness_cam + 0.8 * ripeness_tello;
      Sent_Data.total_ripeness = total_ripeness;

      // Send ripeness data via ESP-NOW
      SendPred();
      goToSleep();
    }
  } else {
    Serial.println("The ripeness is less than 30%.");
    // Step 11: Go to deep sleep for 5 minutes
    goToSleep();
  }
}

// ======================================= Drone Control =======================================
// Function to send commands to the Tello drone
void sendCommand(char *cmd) {
  udp.beginPacket(TELLO_IP, TELLO_PORT);
  udp.write((uint8_t *)cmd, strlen(cmd));
  udp.endPacket();
  Serial.printf("Sent command: %s\n", cmd);
}

// Function to control Tello drone for basic operations
void control_drone_TELLO() {
  sendCommand("command");
  delay(1000);
  sendCommand("takeoff");
  delay(5000);  // Stay in the air for 5 seconds
  sendCommand("land");
}

// ======================================= Main Loop =======================================
// The main loop remains empty as all operations are performed once during setup
void loop() {
  // Nothing to do here
}

/*
 * ===============================================
 * Project: ESP32 Bluetooth & WiFi Integration
 * Copyright (c) 2024 Oleg Shovkovyy
 * Date:    19-10-2024
 * Version: 1.0
 * Description: This program implements a system for fruit ripeness detection by establishing 
 *              communication between ESP32 devices using ESP-NOW. The ESP32 receives data 
 *              from an ESP-CAM, processes the ripeness information, and displays it on the serial monitor. 
 *              The system is designed to efficiently transmit ripeness values while ensuring reliable communication.
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


const char *ssid_wifi = "xxxxxxxxxxxx";
const char *pass_wifi = "xxxxxxxxxxxx";
const char *ssid_tello = "TELLO-xxxxxxxxxxxxxx";
const char *pass_tello = "";
WiFiUDP udp;
const char *TELLO_IP = "192.168.10.1";
const int TELLO_PORT = 8889;
const int LOCAL_PORT = 9000;
float ripeness_cam = 0;
float ripeness_tello = 0;
float total_ripeness = 0;
void setup() {
  Serial.begin(9600);
  //step 1
  initCamera();
  delay(100);
  init_EspNow();
  init_SD();
  delay(100);
  clearSDCardContent();
  //connect to local netwrok step 2
  //if the esp failed to connect within 20sec it will go to deep sleep -->Step11
  //Deep sleep will reset everything and restart the setup function
  if (!wifi_connect_ap(ssid_wifi, pass_wifi)) {
    goToSleep();
  }
  //get the ripness from esp-cam Step 3
  captureAndSaveImage();
  ripeness_cam = getRipenessFromImage("/esp-cam.jpg");
  clearSDCardContent();
  if (ripeness_cam >= 30) {
    Serial.println("the Ripeness is bigger than 30% ");
    //step 4
    wifi_disconnect(ssid_wifi);
    //wait 20 sec
    delay(20 * 1000);
    //connect to tello
    if (!wifi_connect_ap(ssid_tello, pass_tello)) {
      Serial.println("cannot connect to tello sending ripeness_cam only ");
      // go to step 9
      total_ripeness = ripeness_cam;
      if (!wifi_connect_ap(ssid_wifi, pass_wifi)) {
        goToSleep();
      }
      Sent_Data.total_ripeness = total_ripeness;
      // Send total_ripeness via ESP-NOW
      SendPred();
      goToSleep();
    } else {
      //step 5
      udp.begin(TELLO_PORT);
      //add your logic to control the tello and save image in sd card
      captureAndSaveImage();
      Serial.println("calculate ripness form tello ");
      wifi_disconnect(ssid_tello);
      if (!wifi_connect_ap(ssid_wifi, pass_wifi)) {
        goToSleep();
      }
      ripeness_tello = processTelloImages();
      total_ripeness = 0.2 *ripeness_cam + 0.8*ripeness_tello ;
      Sent_Data.total_ripeness = total_ripeness;
      // Send total_ripeness via ESP-NOW
      SendPred();
      goToSleep();
    }

  } else {
    Serial.println("the Ripeness is less than 30% ");
    // got to sleep for 5 min --> step 11
    goToSleep();
  }
}


void sendCommand(char *cmd) {
  udp.beginPacket(TELLO_IP, TELLO_PORT);
  udp.write((uint8_t *)cmd, strlen(cmd));
  udp.endPacket();
  Serial.printf("Sent command: %s\n", cmd);
}



void control_drone_TELLO() {
  sendCommand("command");
  delay(1000);
  sendCommand("takeoff");
  delay(5000);  // Stay in the air for 5 seconds
  sendCommand("land");
}

void loop() {
  //nothing to do as everythin is done only once upon startup
}

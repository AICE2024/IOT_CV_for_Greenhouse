/*
 * =======================================================================================
 * Project: ESP32 Bluetooth Tomato Ripeness Detection
 * Copyright (c) 2024 Oleg Shovkovyy
 * Date:    21.09.2024
 * Version: 1.0
 * Description: This program implements a system for detecting tomato ripeness using an ESP32. 
 *              It handles Bluetooth communication to send ripeness data, processes images 
 *              captured by an ESP32-CAM, interacts with Roboflow for ripeness analysis, and 
 *              controls a Tello drone to capture additional images.
 * =======================================================================================
 */
 /*
 * DISCLAIMER:
 * This code is shared for educational and demonstration purposes only.
 * Ensure you replace placeholder values with your own secure credentials before deploying.
 */

// ====================================== Includes ========================================
#include "esp_camera.h"
#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"

#include "FS.h" // For SD card functionality
#include "SD_MMC.h" // For SD card functionality
#include "soc/soc.h" // Disable brownout detector
#include "soc/rtc_cntl_reg.h" // Disable brownout detector
#include "driver/rtc_io.h"

#include "BluetoothSerial.h"
#include "WiFiUdp.h"
#include "WiFi.h"
#include "HTTPClient.h"
#include "ArduinoJson.h"
#include "Base_64.h"

// ===================================== Configuration =====================================
const char *ssid_wifi = "xxxxxxxxxxxxxxxxx";
const char *pass_wifi = "xxxxxxxxxxxxxxxxx";
const char *ssid_tello = "xxxxxxxxxxxxxx";
const char *pass_tello = "";

BluetoothSerial SerialBT;
uint8_t SMA[6] = {0xCC, 0x7B, 0x5C, 0xA7, 0x0A, 0x81}; // MAC address of ESP32-WiFi

WiFiUDP udp;
const char* TELLO_IP = "192.168.10.1";
const int TELLO_PORT = 8889;
const int LOCAL_PORT = 9000;

const char *serverName = "https://detect.roboflow.com/xxxxxxxxxxxxxxxxxxxxx";

unsigned long previousMillis = 0;  // Store the last time an action was performed
const unsigned long interval = 4 * 60 * 1000;  // 4 minutes
const unsigned long interval_wifi = 2 * 60 * 100;

float ripeness_cam;
float ripeness_tello = 0;

// ====================================== Functions ========================================
void setup() {
  Serial.begin(9600);
  wifi_connect_ap(ssid_wifi, pass_wifi);

  udp.begin(LOCAL_PORT);
  initCamera();
}
bool tello_wifi = true;

// Connect to a WiFi access point
void wifi_connect_ap(const char *ssid, const char *pass) {
  tello_wifi = true;
  unsigned long currentMillis = millis();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if((millis() - currentMillis) > interval_wifi) {
      tello_wifi = false;
      break;
    }
  }
  Serial.println("");
  Serial.println("WiFi connected.");
}

// Send a command to the Tello drone
void sendCommand(char* cmd) {
  udp.beginPacket(TELLO_IP, TELLO_PORT);
  udp.write((uint8_t*)cmd, strlen(cmd));
  udp.endPacket();
  Serial.printf("Sent command: %s\n", cmd);
}

// Initialize the ESP32-CAM
void initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    ESP.restart();
  }
}

// Convert a captured photo to Base64 format
String Photo2Base64() {
  camera_fb_t *fb = NULL;
  fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    return "";
  }
  String imageFile = "";
  char *input = (char *)fb->buf;
  char output[base_64_enc_len(3)];
  for (int i = 0; i < fb->len; i++) {
    base_64_encode(output, (input++), 3);
    if (i % 3 == 0)
      imageFile += String(output);
  }
  esp_camera_fb_return(fb);
  return imageFile;
}

// Send an image to Roboflow and get ripeness data
float http_robotflow(String raw) {
  HTTPClient http;
  http.begin(serverName);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  int httpResponseCode = http.POST(raw);
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println(httpResponseCode);
    Serial.println("Image uploaded successfully!");
    return json_data(response);
  } else {
    Serial.print("Error on sending POST: ");
    Serial.println(httpResponseCode);
  }
  http.end();
  return 0;
}

// Parse JSON response from Roboflow
float json_data(String response) {
  StaticJsonDocument<2048> doc;
  DeserializationError error = deserializeJson(doc, response);

  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return 0;
  }

  JsonArray predictions = doc["predictions"];
  int greenCount = 0;
  int ripeCount = 0;
  float ripeness_json = 0;

  for (JsonObject prediction : predictions) {
    const char *classType = prediction["class"];

    if (strcmp(classType, "green") == 0) {
      greenCount++;
    } else if (strcmp(classType, "unripe") == 0 || strcmp(classType, "ripe") == 0) {
      ripeCount++;
    }
  }
  int sum = ripeCount + greenCount;
  ripeness_json = ripeCount ? ripeCount * 100.0 / sum : 0;

  Serial.print("Total tomatoes: ");
  Serial.print(sum);
  Serial.print("; Ripe tomatoes: ");
  Serial.println(ripeCount);
  Serial.print("Ripeness: ");
  Serial.println(ripeness_json);
  return ripeness_json;
}

// ======================================= Main ============================================
void loop() {
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= interval / 10) {
    previousMillis = currentMillis;
    String image_str = Photo2Base64();
    float ripeness = http_robotflow(image_str);

    if (ripeness > 30) {
      WiFi.disconnect(true);
      control_drone_TELLO();
      ripeness = (ripeness + ripeness_tello) / 2;
      delay(1000);
      wifi_connect_ap(ssid_wifi, pass_wifi);
    }

    if (!SerialBT.begin("ESP32-BT-Master", true)) {
      Serial.println("Error initializing Bluetooth");
      ESP.restart();
    }

    bool connected = SerialBT.connect(SMA);
    if (connected) {
      Serial.println("Connected Successfully!");
      SerialBT.print(ripeness);
    } else {
      Serial.println("Connection Failed!");
      ESP.restart();
    }
    delay(1000);
    SerialBT.end();
    delay(1000);
  }
}

/*
 * ===============================================
 * Project: ESP32 Bluetooth & WiFi Integration for Fruit Ripeness Detection
 * Copyright (c) 2024 Oleg Shovkovyy
 * Date:      11-09-2024
 * Version:   1.0
 * Description: This project integrates Bluetooth and Wi-Fi functionalities on the ESP32 platform 
 *              to create a system for detecting the ripeness of fruits. The system communicates 
 *              with a cloud-based machine learning model via HTTP POST requests to analyze images 
 *              and predict fruit ripeness levels. Bluetooth is used to receive commands and share 
 *              updates, while Wi-Fi enables connectivity to cloud services. The system is designed 
 *              for real-time applications, providing a scalable solution for agricultural automation.
 * ===============================================
 */

 /*
 * DISCLAIMER:
 * This code is shared for educational and demonstration purposes only.
 * Ensure you replace placeholder values with your own secure credentials before deploying.
 */

#include "BluetoothSerial.h"
#include "WiFi.h"
#include "time.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` and enable it
#endif
#if !defined(CONFIG_BT_SPP_ENABLED)
#error Serial Bluetooth not available or not enabled. It is only available for the ESP32 chip.
#endif

// ====== Global Variables ======
// Bluetooth Serial Object (Handle)
BluetoothSerial SerialBT;
String device_name = "ESP32-BT-Slave";

const char 	*ssid = "xxxxxxxxxxxxxxxxx";
const char 	*password = "xxxxxxxxxxxxxxxxxxx";
const char 	*serverName = "https://detect.roboflow.com/xxxxxxxxxxxxxxxxxxxxxxxxxxx";
const char 	*ntpServer = "pool.ntp.org";
const long 	gmtOffset_sec = 25200; // GMT+7 => 3600*7 = 25200
const int 	daylightOffset_sec = 0;

float ripeness = 0;

// ====== Setup Function ======
void setup()
{
  Serial.begin(115200);
  // ====== WiFi Connection ======
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected.");

  // ====== Configure Time ======
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  // ====== Initialize Bluetooth ======
  bt_Init();
}

// ====== Bluetooth Initialization ======
void bt_Init()
{
  SerialBT.begin(device_name); // Bluetooth device name
  Serial.printf("The device with name \"%s\" is started.\nNow you can pair it with Bluetooth!\n", device_name.c_str());
  SerialBT.register_callback(btCallback);
}

// ====== Bluetooth Callback ======
void btCallback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
  if (event == ESP_SPP_SRV_OPEN_EVT)
  {
    Serial.println("Client Connected!");
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo))
    {
      Serial.println("Failed to obtain time");
      ESP.restart();
    }
    SerialBT.println(&timeinfo, "%H");
    SerialBT.flush();
  }
}

// ====== HTTP POST Request ======
void http_post(String raw)
{
  HTTPClient http;
  http.begin(serverName);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  int httpResponseCode = http.POST(raw);
  if (httpResponseCode > 0)
  {
    String response = http.getString();
    Serial.println(httpResponseCode);
    Serial.println(response);

    // ====== Process JSON Data ======
    json_data(response);
  }
  else
  {
    Serial.print("Error on sending POST: ");
    Serial.println(httpResponseCode);
  }
  http.end();
}

// ====== JSON Parsing ======
void json_data(String response)
{
  StaticJsonDocument<2048> doc;
  DeserializationError error = deserializeJson(doc, response);

  if (error)
  {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }

  JsonArray predictions = doc["predictions"];
  int greenCount = 0;
  int ripeCount = 0;

  for (JsonObject prediction : predictions)
  {
    const char *classType = prediction["class"];

    // Check class and update counters
    if (strcmp(classType, "green") == 0)
    {
      greenCount++;
    }
    else if (strcmp(classType, "unripe") == 0)
    {
      ripeCount++;
    }
    else if (strcmp(classType, "ripe") == 0)
    {
      ripeCount++;
    }
  }
  int sum = ripeCount + greenCount;
  if (ripeCount)
  {
    ripeness = ripeCount / (ripeCount + greenCount);
  }
  else
  {
    ripeness = 0;
  }

  // ====== Output Results ======
  Serial.print("Total green: ");
  Serial.println(greenCount);
  Serial.print("Total ripe: ");
  Serial.println(ripeCount);
  Serial.print("Ripeness of fruit: ");
  Serial.println(ripeness);
}

// ====== Main Loop ======
int bt_wait = 0; 					// A flag to indicate whether Bluetooth data is being processed
void loop()
{
  String data_bt = "";				// Variable to store incoming Bluetooth data
  while (SerialBT.available())
  {
    bt_wait = 1;					// Set the flag when data is being read from Bluetooth
    char c = SerialBT.read();    	// Read a character from SerialBT
    data_bt += c; 					// Append the character to the data_bt string
  }

  if (bt_wait && data_bt > "can_not")
  {
    bt_wait = 0;
    delay(2000);
    SerialBT.end();

    // ====== HTTP POST Action ======
    Serial.println("HTTP POST to Roboflow");
    http_post(data_bt);
    delay(1000);
    bt_Init();
  }
  delay(1000);
}

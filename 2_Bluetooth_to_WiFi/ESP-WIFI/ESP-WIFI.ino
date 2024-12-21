/*
 * =======================================================================================
 * Project: ESP32 Bluetooth Tomato Ripeness Detection
 * Copyright (c) 2024 Oleg Shovkovyy
 * Date: 	  21.09.2024
 * Version: 1.0
 * Description: This program handles Bluetooth communication for receiving tomato ripeness data 
 *              from a connected client. The ESP32 processes the ripeness value and displays 
 *              it on the serial monitor.
 * =======================================================================================
 */

// ====================================== Includes ======================================
#include "BluetoothSerial.h" // Include Bluetooth library for ESP32 communication

// ============================= Bluetooth Configuration ================================

String device_name = "ESP32-BT-Server"; // Bluetooth device name

// Check if Bluetooth is enabled on the ESP32
#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

// Check if Serial Port Profile (SPP) is enabled for Bluetooth
#if !defined(CONFIG_BT_SPP_ENABLED)
#error Serial Port Profile for Bluetooth is not available or not enabled. It is only available for the ESP32 chip.
#endif

BluetoothSerial SerialBT;  // Initialize the Bluetooth Serial object
// =============================== Setup Function ====================================
void setup() {
  Serial.begin(9600); // Start serial communication at 9600 baud rate
  SerialBT.begin(device_name);  // Initialize Bluetooth with the device name

  // Uncomment the following line to delete all paired Bluetooth devices upon startup
  // SerialBT.deleteAllBondedDevices(); 
  
  // Print message indicating Bluetooth is ready and awaiting pairing
  Serial.printf("The device with name \"%s\" is started.\nNow you can pair it with Bluetooth!\n", device_name.c_str());

  // Register the Bluetooth callback function to handle events
  SerialBT.register_callback(btCallback);
}

// ================================ Bluetooth Callback =================================
/*
 * Callback function that handles Bluetooth events:
 * - ESP_SPP_SRV_OPEN_EVT: Client connects to the ESP32
 * - ESP_SPP_DATA_IND_EVT: Data is received from the connected client
 */
void btCallback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param){
  if(event == ESP_SPP_SRV_OPEN_EVT){  // Client has connected
    Serial.println("Client Connected!");
  }else if(event == ESP_SPP_DATA_IND_EVT){  // Data received from client
    Serial.printf("ESP_SPP_DATA_IND_EVT len=%d, handle=%d\n\n", param->data_ind.len, param->data_ind.handle);

    // Convert the incoming data to a string
    String stringRead = String(*param->data_ind.data);

    // Convert the string to an integer representing the tomato ripeness value
    int ripeness = stringRead.toInt() - 48;  // Adjust the value as needed

    // If the ripeness value is valid (greater than or equal to 0), print it
    if(ripeness>=0){
      Serial.printf("Tomatos ripeness: %d %\n", ripeness);
      delay(1000);  // Added a short delay for stability
    }
  }
}

// ==================================== Loop Function ===================================
void loop() {
  // No continuous looping logic is needed in this case; waiting for Bluetooth events
}

#ifndef FUNCTIONS_H
#define FUNCTIONS_H
#include "common.h"
#include "camera_pins.h"
#include "Base_64.h"
// ESP-NOW parameters
extern uint8_t espMACAddress[];
extern uint8_t espCamMACAddress[];
extern esp_now_peer_info_t peerInfo;

// Size of a single packet for ESP-NOW
typedef struct struct_message {
  char data[100];  // Data buffer for messages
  float total_ripeness;
} struct_message;
extern struct_message Sent_Data;
extern struct_message response;
// Define sleep duration (5 minutes) in microseconds
extern const uint64_t sleepTime;
extern const char* serverName;
// Define interval for Wi-Fi connection (20 seconds) in milliseconds
extern const unsigned long interval_wifi;
// Callback function for ESP-NOW
void OnDataSent(const uint8_t* mac_addr, esp_now_send_status_t status);
void OnDataRecv(const uint8_t* mac, const uint8_t* incomingData, int len);
// Initialization functions
void init_EspNow();
void initCamera();
void init_SD();
void goToSleep();
bool wifi_connect_ap(const char *ssid, const char *pass);
String Photo2Base64(const char* path);
float http_robotflow(String raw);
float json_data(String response);
void clearSDCardContent();
void wifi_disconnect(const char *ssid);
void SendPred();
void captureAndSaveImage();
float getRipenessFromImage(const String& imagePath);
float processTelloImages();
#endif 
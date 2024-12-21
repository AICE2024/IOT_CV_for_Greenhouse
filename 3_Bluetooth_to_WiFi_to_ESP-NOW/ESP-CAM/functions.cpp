 /*
 * DISCLAIMER:
 * This code is shared for educational and demonstration purposes only.
 * Ensure you replace placeholder values with your own secure credentials before deploying.
 */
 

#include "functions.h"
// Define sleep duration (5 minutes) in microseconds
const uint64_t sleepTime = 5 * 60 * 1000000;  // 5 minutes in microseconds
const char *serverName= "https://detect.roboflow.com/xxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
// Define interval for Wi-Fi connection (20 seconds) in milliseconds
const unsigned long interval_wifi = 20 * 1000;  // 20 seconds in milliseconds
uint8_t espMACAddress[] = { 0xCC, 0x7B, 0x5C, 0xA7, 0x0A, 0x81 };
uint8_t espCamMACAddress[] = { 0x30, 0xC9, 0x22, 0xE2, 0xC9, 0xDA };
esp_now_peer_info_t peerInfo;
struct_message Sent_Data;
struct_message response;
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
  // init with high specs to pre-allocate larger buffers
  if (psramFound()) {
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_CIF;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    ESP.restart();
  }
}
// Initialize the SD Card
void init_SD() {
  if (!SD_MMC.begin("/sdcard", true)) {
    Serial.println("SD Card Mount Failed");
    return;
  }
  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("No SD Card attached");
    return;
  }
  Serial.println("SD Card initialized successfully.");
}
// Initialize ESP-NOW
void init_EspNow() {
  WiFi.mode(WIFI_STA);
  esp_err_t err = esp_wifi_set_mac(WIFI_IF_STA, &espCamMACAddress[0]);
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
  memcpy(peerInfo.peer_addr, espMACAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }
}

// Callback function for ESP-NOW
void OnDataSent(const uint8_t* mac_addr, esp_now_send_status_t status) {
  Serial.print("Last Packet Send Status: ");
  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.println("Delivery Success");
  } else {
    Serial.println("Delivery Fail");
  }
}

// Callback function that will be executed when data is received
void OnDataRecv(const uint8_t* mac, const uint8_t* incomingData, int len) {
  memcpy(&response, incomingData, sizeof(response));
  Serial.printf("Received message: %s\n", response.data);
  
  }
void SendPred() {
  esp_err_t result = esp_now_send(espMACAddress, (uint8_t*)&Sent_Data, sizeof(Sent_Data));
  if (result == ESP_OK) {
    Serial.println("Sent with success");
  } else {
    Serial.println("Error sending the data");
  }
}
void goToSleep() {
  esp_sleep_enable_timer_wakeup(sleepTime);
  Serial.print("Sleep Time: ");
  Serial.println(sleepTime);  // Print sleep time in microseconds
  delay(100);
  esp_deep_sleep_start();
}
bool wifi_connect_ap(const char *ssid, const char *pass) {
  unsigned long currentMillis = millis();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, pass);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    
    // Check if the connection attempt has timed out
    if ((millis() - currentMillis) > interval_wifi) {
      Serial.println("Connection timed out.");
      return false;  // Return false if connection failed
    }
  }

  Serial.println("");
  Serial.println("WiFi connected.");
  return true;  // Return true if connected
}

void wifi_disconnect(const char *ssid) {
  Serial.print("Disconnecting from WiFi...");
  Serial.println(ssid);
  // Disconnect from the Wi-Fi network
  WiFi.disconnect();
  
  // Wait a moment for the disconnection to take effect
  delay(1000);
  
  // Check the connection status
  bool isDisconnected = (WiFi.status() != WL_CONNECTED);
  
  // Print disconnection status
  if (isDisconnected) {
    Serial.println("Disconnected from WiFi.");
  } else {
    Serial.println("Failed to disconnect from WiFi.");
  }
}
void captureAndSaveImage() {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    return;
  }

  String path = "/esp-cam.jpg";

  File file = SD_MMC.open(path.c_str(), FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file in writing mode");
    esp_camera_fb_return(fb);
    return;
  }

  file.write(fb->buf, fb->len);
  Serial.printf("Image saved to: %s\n", path.c_str());

  file.close();
  esp_camera_fb_return(fb);
}

String Photo2Base64(const char* filePath) {
  // Open the image file from the SD card
  File file = SD_MMC.open(filePath, FILE_READ);
  if (!file) {
    Serial.println("Failed to open file for reading");
    return "";
  }

  // Check the file size and allocate a buffer
  size_t fileSize = file.size();
  uint8_t* buffer = (uint8_t*)malloc(fileSize);
  if (!buffer) {
    Serial.println("Failed to allocate memory for file");
    file.close();
    return "";
  }

  // Read the file into the buffer
  file.read(buffer, fileSize);
  file.close(); // Close the file after reading

  // Prepare Base64 encoding
  String imageFile = "";
  char* input = (char*)buffer;
  char output[base_64_enc_len(3)];
  for (int i = 0; i < fileSize ;i++) {
    base_64_encode(output, (input++), 3);
    if (i % 3 == 0)
      imageFile += String(output);
  }

  // Free the buffer
  free(buffer);

  return imageFile;
}
float http_robotflow(String raw) {
  HTTPClient http;
  http.begin(serverName);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  int httpResponseCode = http.POST(raw);
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println(httpResponseCode);
    Serial.println("Image upload successfully!");
    return json_data(response);
  } else {
    Serial.print("Error on sending POST: ");
    Serial.println(httpResponseCode);
  }
  http.end();
  return 0;
}
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
    const char* classType = prediction["class"];

    // Check class and counter
    if (strcmp(classType, "green") == 0) {
      greenCount++;
    } else if (strcmp(classType, "unripe") == 0) {
      ripeCount++;
    } else if (strcmp(classType, "ripe") == 0) {
      ripeCount++;
    }
  }
  int sum = ripeCount + greenCount;
  if (ripeCount) {
    ripeness_json = ripeCount * 100 / (ripeCount + greenCount);
  } else {
    ripeness_json = 0;
  }
  // Print results to Serial Monitor
  Serial.print("All tomatoes: ");
  Serial.print(greenCount + ripeCount);
  Serial.print("; ripe tomatoes: ");
  Serial.println(ripeCount);
  Serial.print("Ripeness of tomatoes: ");
  Serial.println(ripeness_json);
  return ripeness_json;
}

void clearSDCardContent() {
  File root = SD_MMC.open("/");  // Open the root directory of the SD card
  if (!root) {
    Serial.println("Unable to open root directory");
    return;
  }

  if (!root.isDirectory()) {
    Serial.println("Invalid SD card");
    return;
  }

  Serial.println("Deleting all content on the SD card...");

  // Iterate through all files and folders on the SD card
  File file = root.openNextFile();
  while (file) {
    String fileName = String("/") + file.name();
    if (file.isDirectory()) {
      SD_MMC.rmdir(fileName.c_str());  // Delete the directory
      Serial.print("Deleted directory: ");
    } else {
      SD_MMC.remove(fileName.c_str());  // Delete the file
      Serial.print("Deleted file: ");
    }
    Serial.println(fileName);

    file = root.openNextFile();  // Open the next file
  }

  Serial.println("All content on the SD card has been deleted.");
}

// Function to convert image to Base64 and get ripeness
float getRipenessFromImage(const String& imagePath) {
    String image_str = Photo2Base64(imagePath.c_str());
    return http_robotflow(image_str);
}
float processTelloImages(){
     // Variable to store total ripeness
    float totalRipeness = 0.0;
    int imageCount = 0;

    // Open the directory on the SD card
    File root = SD_MMC.open("/");
    File file = root.openNextFile();

    // Loop through all files in the directory
    while (file) {
        // Check if the file is a JPEG image
        String fileName = file.name();
         if (fileName.endsWith(".jpg") || fileName.endsWith(".jpeg")) {
            Serial.printf("Processing image: %s\n", file.name());

            // Get ripeness from the image
            float ripeness = getRipenessFromImage(file.path());
            if (ripeness >= 0) { // Check if the ripeness value is valid
                totalRipeness += ripeness;
                imageCount++;
            }

            file.close(); // Close the file after processing
        }
        file = root.openNextFile(); // Get the next file
    }

    // Calculate average ripeness if images were processed
    if (imageCount > 0) {
        float averageRipeness = totalRipeness / imageCount;
        Serial.printf("Average Ripeness: %.2f\n", averageRipeness);
        return averageRipeness;
    } else {
        Serial.println("No valid images found.");
        return 0;
    }
}
/*
 * =======================================================================================
 * Project: Automated Tomato Ripeness Analysis and Drone-Assisted Monitoring System
 * Author:  AICE2024
 * Date:    23-11-2024
 * Version: 1.0
 * Description: This program sets up an ESP32 device for various operations, including:
 *              - Capturing images via ESP32-CAM to analyze tomato ripeness using a remote server
 *              - Drone-based video recording for extended monitoring and analysis
 *              - Data communication using ESP-NOW to transmit results to a receiver
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

// ====== Wi-Fi and Drone Credentials ======

// Tello drone credentials
const char *TELLO_SSID = "TELLO-xxxxxxxxx";
const char *TELLO_PASSWORD = "";

// Local network credentials
const char *WIFI_SSID = "xxxxxxxxxxxxxx";
const char *WIFI_PASSWORD = "x";

// ====== GCP Server Details ======
// GCP server details
const char *host = "xxxxxxxxxxxxxxxx"; // Server host
const int httpsPort = 443;                                 // Server port

// Add confidence and overlap parameters
const float CONFIDENCE_THRESHOLD = 45.0; // Confidence threshold for object detection
const float OVERLAP_THRESHOLD = 25.0;    // Overlap threshold for object detection

// Global variables for timing
unsigned long lastRunTime = 0;
const unsigned long RUN_INTERVAL = 60000; // Run every 1 minutes

// Instance of the InferenceHandler library
InferenceHandler inferenceHandler(WIFI_SSID, WIFI_PASSWORD, host, httpsPort);

// Instance of the TelloESP32 library
TelloESP32 tello;
// Video file instance for saving tello video stream
File videoFile;

std::vector<String> recordedVideoPaths; // Store paths of recorded videos
String currentVideoPath;

// Vector stoing the ripeness results of each inference request
std::vector<std::pair<String, float>> ripenessResults; // Store <filename, ripeness%> pairs

// SSID and MAC addresses array for receiver (Auto find receiver MAC address using SSID)
const char *RECEIVER_SSID = "ESP32_RECEIVER";
uint8_t receiverMacAddress[6];

// helper functions prototypes
String getNextFilePath(const String &folderName, const String &prefix, const String &extension);
void handleVideoData(const uint8_t *buffer, size_t size);
void startNewVideoRecording(const String &videoPath);
void stopVideoRecording();
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
bool findReceiverMac();
void initEspNow();

void setup()
{
    Serial.begin(115200);
}

void loop()
{
    unsigned long currentTime = millis();

    // Check if it's time to run the sequence
    if (currentTime - lastRunTime >= RUN_INTERVAL)
    {
        Serial.println("Starting operation...");
        // Initialize SD Card
        if (!SD_MMC.begin("/sdcard", true))
        {
            Serial.println("SD Card Mount Failed, Operations halted");
            return;
        }
        Serial.println("SD Card mounted successfully");

        /************* Capturing Image from esp cam  ******************/
        // Initialize Camera
        if (!initCamera())
        {
            Serial.println("Camera initialization failed");
        }
        else
        {
            Serial.println("Camera initialized successfully");
            // capture new image and save to SD card
            String imagePath = getNextFilePath("/camImages", "camImg_", ".jpg");
            if (captureAndSaveImage(imagePath))
            {
                Serial.println("Image captured and saved to: " + imagePath);
                // deinit camera
                deinitCamera();

                /************* Testing tomato ripeness on the camera image  ******************/
                // Initialize the inference handler
                if (!inferenceHandler.begin())
                {
                    Serial.println("InferenceHandler initialization failed!");
                    return;
                }
                Serial.println("InferenceHandler initialized.");

                // Process camera image
                InferenceResult imageResult;
                if (inferenceHandler.requestInference(imagePath.c_str(), CONFIDENCE_THRESHOLD, OVERLAP_THRESHOLD, imageResult))
                {
                    // Print results
                    Serial.println("Camera Image Analysis:");
                    Serial.printf("Total Objects: %d\n", imageResult.totalObjects);
                    Serial.printf("Ripe Tomatoes: %d\n", imageResult.ripeCount);
                    Serial.printf("Unripe Tomatoes: %d\n", imageResult.unripeCount);
                    Serial.printf("Green Tomatoes: %d\n", imageResult.greenCount);
                    Serial.printf("Ripeness Percentage: %.2f%%\n", imageResult.ripenessPercentage);

                    ripenessResults.push_back({imagePath, imageResult.ripenessPercentage}); // Store ripeness result (will be used for esp-now transmission)

                    // Check ripeness threshold
                    if (imageResult.ripenessPercentage <= 30.0)
                    {
                        Serial.println("Tomatoes are not ripe enough, waiting for next runtime...");
                        inferenceHandler.end();
                        return;
                    }
                    Serial.println("Tomatoes are ripe enough");
                }
                else
                {
                    Serial.println("Image inference failed");
                    inferenceHandler.end();
                    return;
                }

                // Clean up inference handler
                inferenceHandler.end();
            }
            else
            {
                Serial.println("Failed to capture and save image");
            }
        }

        /************* Tello operations ******************/
        // Connect to Tello drone
        if (tello.connect(TELLO_SSID, TELLO_PASSWORD, 10000))
        {
            Serial.print("Battery: ");
            Serial.print(tello.getBattery());

            // setting video stream settings
            tello.onVideoStreamData(handleVideoData);
            if(!tello.setVideoBitrate(TelloESP32::BITRATE_1MBPS)) // Set video bitrate to 1 Mbps
            {
                Serial.println("Failed to set video bitrate");
            }
            if(!tello.setVideoFPS(TelloESP32::FPS_5))             // Set video fps to 5
            {
                Serial.println("Failed to set video fps");
            }

            Serial.println("Starting flight sequence...");
            // tello.takeoff();

            // Record first video
            currentVideoPath = getNextFilePath("/telloVideos", "telloVideo_", ".h264");
            recordedVideoPaths.push_back(currentVideoPath); // Store path
            startNewVideoRecording(currentVideoPath);
            // tello.right(50);
            delay(5000);
            stopVideoRecording();
            delay(3000);
            // tello.rotateClockwise(90);
            // Move drone and record second video
            // tello.up(50);
            currentVideoPath = getNextFilePath("/telloVideos", "telloVideo_", ".h264");
            recordedVideoPaths.push_back(currentVideoPath); // Store path
            startNewVideoRecording(currentVideoPath);
            // tello.right(50);
            delay(5000);
            stopVideoRecording();
            // tello.land();
            Serial.println("Flight sequence completed.");
            // Disconnect from Tello drone
            tello.disconnect();

            /************* Process all recorded videos ******************/
            Serial.printf("\nProcessing %d recorded videos\n", recordedVideoPaths.size());

            if (!inferenceHandler.begin())
            {
                Serial.println("InferenceHandler initialization failed!");
                return;
            }
            Serial.println("InferenceHandler initialized.");

            // Process each recorded video
            for (const String &videoPath : recordedVideoPaths)
            {
                Serial.printf("\nProcessing video: %s\n", videoPath.c_str());

                InferenceResult videoResult;
                if (inferenceHandler.requestInference(videoPath.c_str(), CONFIDENCE_THRESHOLD, OVERLAP_THRESHOLD, videoResult))
                {
                    Serial.printf("Video Analysis Results:\n");
                    Serial.printf("Frames Processed: %d\n", videoResult.frameCount);
                    Serial.printf("Total Objects: %d\n", videoResult.totalObjects);
                    Serial.printf("Ripe Tomatoes: %d\n", videoResult.ripeCount);
                    Serial.printf("Unripe Tomatoes: %d\n", videoResult.unripeCount);
                    Serial.printf("Green Tomatoes: %d\n", videoResult.greenCount);
                    Serial.printf("Overall Ripeness: %.2f%%\n", videoResult.ripenessPercentage);
                    ripenessResults.push_back({videoPath, videoResult.ripenessPercentage}); // Store ripeness result
                }
                else
                {
                    Serial.printf("Failed to process video: %s\n", videoPath.c_str());
                }
            }

            inferenceHandler.end();
        }
        else
        {
            Serial.println("Failed to connect to Tello drone, skipping video recording");
        }

        /************* Send ripeness results to esp32 via esp-now ******************/
        initEspNow();

        // Check if we have any results to send
        if (ripenessResults.empty())
        {
            Serial.println("No ripeness results to send");
            return;
        }

        // Create JSON array from results
        StaticJsonDocument<1024> doc;
        JsonArray array = doc.to<JsonArray>();

        // Only add valid results
        for (const auto &result : ripenessResults)
        {
            // Check for empty filename or invalid ripeness value
            if (!result.first.isEmpty() && result.second >= 0.0)
            {
                JsonObject obj = array.createNestedObject();
                obj["file"] = result.first;
                obj["ripeness"] = result.second;
            }
            else
            {
                Serial.printf("Skipping invalid result: file='%s', ripeness=%.2f\n",
                              result.first.c_str(), result.second);
            }
        }

        // Only send if we have valid results
        if (array.size() > 0)
        {
            String jsonString;
            serializeJson(doc, jsonString);

            esp_err_t result = esp_now_send(receiverMacAddress,
                                            (uint8_t *)jsonString.c_str(),
                                            jsonString.length());

            if (result == ESP_OK)
            {
                Serial.println("Results sent successfully");
                
                // Clear vectors after successful transmission
                ripenessResults.clear();
                recordedVideoPaths.clear();
                Serial.println("Cleared results and video paths");
            }
            else
            {
                Serial.println("Error sending results");
            }
        }
        else
        {
            Serial.println("No valid results to send");
        }
        Serial.println("Operation Complete, Waiting for next runtime...\n\n");
        lastRunTime = currentTime;
    }
    

}

// Helper function to get next file path with auto-incrementing number
String getNextFilePath(const String &folderName, const String &prefix, const String &extension)
{
    if (!SD_MMC.exists(folderName))
    {
        SD_MMC.mkdir(folderName);
        return folderName + "/" + prefix + "1" + extension;
    }

    File root = SD_MMC.open(folderName);
    if (!root || !root.isDirectory())
    {
        return folderName + "/" + prefix + "1" + extension;
    }

    int maxNumber = 0;
    File file = root.openNextFile();

    while (file)
    {
        String fileName = String(file.name());
        if (fileName.startsWith(prefix) && fileName.endsWith(extension))
        {
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

// Function to start a new Tello stream recording
void startNewVideoRecording(const String &videoPath)
{
    Serial.print("Creating video file: ");
    Serial.println(videoPath);

    // Create directory if it doesn't exist
    String dir = videoPath.substring(0, videoPath.lastIndexOf('/'));
    if (!SD_MMC.exists(dir))
    {
        SD_MMC.mkdir(dir);
    }

    videoFile = SD_MMC.open(videoPath.c_str(), FILE_WRITE);
    if (!videoFile)
    {
        Serial.println("Failed to open video file for writing");
        return;
    }
    delay(500); // Wait before starting new stream
    tello.startVideoStream();
}

// Function to stop the video recording
void stopVideoRecording()
{
    tello.stopVideoStream();
    videoFile.close();
    delay(500); // Wait for file to close
}

// Callback to handle video data stream
void handleVideoData(const uint8_t *buffer, size_t size)
{
    if (videoFile)
    {
        if (videoFile.write(buffer, size) != size)
        {
            Serial.println("Error writing to SD card!");
        }
        else
        {
            Serial.print("*");
        }
    }
}

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    Serial.print("\r\nLast Packet Send Status: ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

void initEspNow()
{
    // Disconnect from any existing WiFi
    WiFi.disconnect();

    // Set WiFi mode to Station
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    // Find receiver's MAC address
    if (!findReceiverMac())
    {
        Serial.println("Receiver not found!");
        return;
    }

    // Initialize ESP-NOW
    if (esp_now_init() != ESP_OK)
    {
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
    peerInfo.ifidx = WIFI_IF_STA; // Changed from ESP_IF_WIFI_STA

    // Add peer with error checking
    esp_err_t addStatus = esp_now_add_peer(&peerInfo);
    if (addStatus != ESP_OK)
    {
        Serial.print("Failed to add peer, error: ");
        Serial.println(addStatus);
        return;
    }

    Serial.println("ESP-NOW initialized successfully");
}

bool findReceiverMac()
{
    Serial.println("Scanning for receiver...");

    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    delay(100);

    int n = WiFi.scanNetworks();
    if (n == 0)
    {
        Serial.println("No networks found");
        return false;
    }

    for (int i = 0; i < n; ++i)
    {
        if (String(RECEIVER_SSID).equals(WiFi.SSID(i)))
        {
            String bssid = WiFi.BSSIDstr(i);
            Serial.print("Receiver found! MAC: ");
            Serial.println(bssid);

            // Convert BSSID string to bytes
            int values[6];
            if (sscanf(bssid.c_str(), "%x:%x:%x:%x:%x:%x",
                       &values[0], &values[1], &values[2],
                       &values[3], &values[4], &values[5]) == 6)
            {
                for (int j = 0; j < 6; j++)
                {
                    receiverMacAddress[j] = (uint8_t)values[j];
                }
                return true;
            }
        }
    }
    return false;
}
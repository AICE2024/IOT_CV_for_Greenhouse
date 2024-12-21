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



#include <Tello.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <SD_MMC.h>
#include <freertos/semphr.h>

const char *networkSSID = "TELLO-xxxxxxxxxxxxxx";
const char *networkPswd = "";
const int VIDEO_PORT = 11111;
const int maxPacketSize = 5120;


Tello tello;
WiFiUDP videoUdp;
File videoFile;

TaskHandle_t controlTaskHandle = NULL;
TaskHandle_t videoTaskHandle = NULL;
SemaphoreHandle_t recordFlagMutex;

int recordCount = 0;
unsigned long recordUntil = 0;

#define recordDuration 10000

void generateNewVideoFile()
{
    String videoPath = "/telloVid_" + String(recordCount++) + ".h264";
    videoFile = SD_MMC.open(videoPath, FILE_WRITE);
    if (!videoFile)
    {
        Serial.println("Failed to open file for writing");
    }
}

void startRecording(unsigned long durationMs)
{
    xSemaphoreTake(recordFlagMutex, portMAX_DELAY);
    recordUntil = millis() + durationMs;
    xSemaphoreGive(recordFlagMutex);
    generateNewVideoFile();
}

bool isRecording()
{
    xSemaphoreTake(recordFlagMutex, portMAX_DELAY);
    bool flag = millis() < recordUntil;
    xSemaphoreGive(recordFlagMutex);
    return flag;
}

void controlTask(void *pvParameters)
{
    while (1)
    {
        Serial.println("Starting route follow sequence...");
        tello.takeoff();                      // -> Takeoff
        tello.up(50);                         // -> up 50
        tello.rotate_anticlockwise(90);       // -> CCW 90
        startRecording(recordDuration); // -> streamon (for 2 seconds)  vid_1
        while (isRecording())
        {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        tello.right(50);                     // -> right 100
        startRecording(recordDuration); // -> streamon (for 2 seconds)  vid_2
        while (isRecording())
        {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        tello.right(50);                     // -> right 100
        startRecording(recordDuration); // -> streamon (for 2 seconds)  vid_3
        while (isRecording())
        {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        tello.rotate_clockwise(180);          // -> CW 180
        startRecording(recordDuration); // -> streamon (for 2 seconds)   vid_4
        while (isRecording())
        {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        tello.right(50);                     // -> right 100
        startRecording(recordDuration); // -> streamon (for 2 seconds)   vid_5
        while (isRecording())
        {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        tello.right(50);                     // -> right 100
        startRecording(recordDuration); // -> streamon (for 2 seconds)   vid_6
        while (isRecording())
        {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        tello.rotate_anticlockwise(90);       // -> CW 90
        tello.land();                         // -> land
        Serial.println("Route follow sequence completed, ending loop.");
        while (1)
        {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
    vTaskDelete(NULL);
}

void videoTask(void *pvParameters)
{
    bool currentlyRecording = false;
    while (true)
    {
        if (isRecording())
        {
            if (!currentlyRecording)
            {
                if (tello.startVideoStream())
                {
                    videoUdp.begin(VIDEO_PORT);
                    currentlyRecording = true;
                    vTaskDelay(pdMS_TO_TICKS(1000)); // buffer time for streaming to stabilize
                    Serial.println("Video Stream Started");
                }
                else
                {
                    Serial.println("Failed to start video stream.");
                }
            }

            if (currentlyRecording && videoUdp.parsePacket())
            {
                uint8_t videoBuffer[maxPacketSize];
                int packetSize = videoUdp.read(videoBuffer, maxPacketSize);
                if (packetSize > 0)
                {
                    if (videoFile.write(videoBuffer, packetSize) != packetSize)
                    {
                        Serial.println("Error writing to SD card!");
                    }
                    else
                    {
                        Serial.print("*");
                    }
                }
            }
        }
        else
        {
            if (currentlyRecording)
            {
                Serial.println("\nStopping video recording.");
                tello.stopVideoStream();
                videoUdp.stop();
                videoFile.close();
                currentlyRecording = false;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

bool initializeSDCard()
{
    if (!SD_MMC.begin("/sdcard"))
    {
        Serial.println("Failed to mount SD card!");
        return false;
    }
    return true;
}
void initializeNetworkAndTello(bool reinit)
{
    WiFi.disconnect(true); 
    WiFi.begin(networkSSID, networkPswd);

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED && !reinit)
    {
        Serial.println(" CONNECTED");
    }
    while (!tello.isInitialised){
        delay(1000);
        tello.init(); 
    }
}

void setup()
{
    Serial.begin(115200);
    if (!initializeSDCard())
    {
        Serial.println("SD card initialization failed. Stopping.");
        while (1);
    }
    Serial.println("SD card initialized.");
    Serial.print("Connecting to Tello ");
    initializeNetworkAndTello(0);
    // Retry command with reinitialization if it fails
    Serial.print("Setting stream fps to 5");
    while(!tello.sendTelloCommandWithRetry("setfps 5")){
        delay(500);
        initializeNetworkAndTello(1); // Reinitialize on failure
    }
    Serial.println("OK");
    Serial.print("Setting 1MBps Bitrate.");
    while(!tello.sendTelloCommandWithRetry("setbitrate 1")){
        delay(500);
        initializeNetworkAndTello(1); // Reinitialize on failure
    }
    Serial.println("OK");
    delay(1000);

    recordFlagMutex = xSemaphoreCreateMutex();

    xTaskCreatePinnedToCore(controlTask, "Control Task", 4096, NULL, 1, &controlTaskHandle, 0);
    xTaskCreatePinnedToCore(videoTask, "Video Task", 8192, NULL, 1, &videoTaskHandle, 1);
}

void loop()
{
}

#include "TelloESP32.h"
#include <Arduino.h>

//  Constants for video settings
const std::string TelloESP32::RESOLUTION_480P = "low";
const std::string TelloESP32::RESOLUTION_720P = "high";
const std::string TelloESP32::FPS_5 = "low";
const std::string TelloESP32::FPS_15 = "middle";
const std::string TelloESP32::FPS_30 = "high";

// Constructor to initialize member variables
TelloESP32::TelloESP32()
    : telloAddr(IPAddress(192, 168, 10, 1)),
      telloPort(8889),
      localPort(9000),
      videoPort(11111),
      commandTimeout(500),
      videoStreamTaskHandle(nullptr),
      receiveResponseTaskHandle(nullptr),
      connectionMonitorTaskHandle(nullptr),
      responseSemaphore(xSemaphoreCreateBinary()), // Initialize semaphore
      connected(false),
      latestResponse("")
{
}

// Connect to Tello drone
bool TelloESP32::connect(const char *ssid, const char *password, unsigned long timeout_ms)
{
    WiFi.begin(ssid, password);
    Serial.print("Connecting to Tello ");
    
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED)
    {
        if (millis() - startTime > timeout_ms)
        {
            Serial.println("\nConnection timeout!");
            WiFi.disconnect(true);
            return false;
        }
        delay(500);
        Serial.print(".");
    }
    
    Serial.println(" Connection established!");
    connected = true;
    
    if (udp.begin(localPort))
    {
        // Create the receiveResponse task
        xTaskCreatePinnedToCore(
            TelloESP32::receiveResponseTask,
            "receiveResponseTask",
            10000,
            this,
            1,
            &receiveResponseTaskHandle,
            0
        );
        
        // Create the connection monitor task
        xTaskCreatePinnedToCore(
            TelloESP32::connectionMonitorTask,
            "connectionMonitorTask",
            10000,
            this,
            1,
            &connectionMonitorTaskHandle,
            0
        );
        
        delay(1000); // Increased delay for Tello response
        return sendCommandWithRetry("command", "ok", 5, 2000, 6000);
    }
    return false;
}

// Disconnect from Tello drone
void TelloESP32::disconnect()
{
    stopVideoStream();
    udp.stop();
    videoUdp.stop();
    connected = false;

    // Clean up tasks if they exist (important to prevent crashes)
    if (receiveResponseTaskHandle)
    {
        vTaskDelete(receiveResponseTaskHandle);
        receiveResponseTaskHandle = nullptr;
    }
    if (connectionMonitorTaskHandle)
    {
        vTaskDelete(connectionMonitorTaskHandle);
        connectionMonitorTaskHandle = nullptr;
    }
    WiFi.disconnect(true);  
}

// Send a command to Tello drone
void TelloESP32::sendCommand(const std::string &command)
{
    Serial.print("Sending command: ");
    Serial.println(command.c_str());
    udp.beginPacket(telloAddr, telloPort);
    udp.write(reinterpret_cast<const uint8_t *>(command.c_str()), command.length());
    udp.endPacket();
}

// Send a command with retries and timeout
bool TelloESP32::sendCommandWithRetry(const std::string &command, const std::string &expectedResponse, int retries, int delayMs, int timeoutMs)
{
    if (!connected)
    {
        Serial.println("Not connected to Tello drone!");
        return false;
    }
    Serial.print("Sending command: \"");
    Serial.print(command.c_str());
    Serial.print("\" ");
    for (int i = 0; i < retries; ++i)
    {
        if (!connected)
        {
            Serial.println("Not connected to Tello drone!");
            return false;
        }
        
        std::string response = sendCommandWithReturn(command, timeoutMs);
        if (!response.empty())
        {
            Serial.print(" Received response: ");
            Serial.println(response.c_str());
            if (response == expectedResponse)
            {
                return true; // Desired response achieved
            }
        }
        else
        {
            Serial.print("Retrying... ");
        }
        delay(delayMs);
    }
    Serial.println("Command failed after max retries.");
    return false;
}

std::string TelloESP32::sendCommandWithReturn(const std::string &command, int timeoutMs)
{
    latestResponse = "";

    udp.beginPacket(telloAddr, telloPort);
    udp.write(reinterpret_cast<const uint8_t *>(command.c_str()), command.length());
    udp.endPacket();

    unsigned long startTime = millis();

    unsigned long lastDotTime = millis();
    // Wait for response or timeout
    while ((millis() - startTime) < timeoutMs)
    {
        if (xSemaphoreTake(responseSemaphore, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            // Received a response
            return latestResponse;
        }
        if ((millis() - lastDotTime) >= 500)
        {
            Serial.print(".");
            lastDotTime = millis();
        }
        vTaskDelay(10); // Small delay to yield
    }
    Serial.println(" Command timed out.");
    return ""; // Return empty string if timeout occurs
}

// Task to receive responses from Tello drone
void TelloESP32::receiveResponseTask(void *pvParameters)
{
    TelloESP32 *tello = static_cast<TelloESP32 *>(pvParameters); // Explicitly cast void pointer
    while (true)
    {
        char buffer[256]; // Increased buffer size
        int packetSize = tello->udp.parsePacket();
        if (packetSize)
        {
            tello->udp.read(buffer, packetSize);         // Read up to packetSize bytes
            buffer[packetSize] = '\0';                   // Null-terminate for string conversion
            tello->latestResponse = std::string(buffer); // Store it in the member variable
            xSemaphoreGive(tello->responseSemaphore);    // Signal that a response has been received
        }
        vTaskDelay(1); // Yield to other tasks
    }
}

// Task to monitor WiFi connection
void TelloESP32::connectionMonitorTask(void *pvParameters)
{
    TelloESP32 *tello = static_cast<TelloESP32 *>(pvParameters); 
    while (true)
    {
        if (WiFi.status() != WL_CONNECTED)
        {
            Serial.println("Connection lost!");
            
            // Set connection state
            tello->connected = false;
            
            // Stop UDP communications
            tello->udp.stop();
            tello->videoUdp.stop();
            
            // Stop video streaming if active
            if (tello->videoStreamTaskHandle != nullptr) {
                vTaskDelete(tello->videoStreamTaskHandle);
                tello->videoStreamTaskHandle = nullptr;
            }
            
            // Call user callback if registered
            if (tello->connectionLostCallback)
            {
                tello->connectionLostCallback();
            }

            // Only receiveResponseTask needs to be stopped
            // Don't stop connection monitor (this task)
            if (tello->receiveResponseTaskHandle)
            {
                vTaskDelete(tello->receiveResponseTaskHandle);
                tello->receiveResponseTaskHandle = nullptr;
            }

            Serial.println("Cleanup complete, awaiting reconnection...");
            
            // Let main code handle reconnection
            break; // Exit task
        }
        vTaskDelay(1000); // Check every second
    }
    
    tello->connectionMonitorTaskHandle = nullptr;
    vTaskDelete(NULL); // Delete this task
}

// Flight control commands
bool TelloESP32::takeoff() { return sendCommandWithRetry("takeoff"); }
bool TelloESP32::land() { return sendCommandWithRetry("land"); }
bool TelloESP32::up(int cm) { return sendCommandWithRetry("up " + std::to_string(constrain(cm, 20, 500))); }
bool TelloESP32::down(int cm) { return sendCommandWithRetry("down " + std::to_string(constrain(cm, 20, 500))); }
bool TelloESP32::left(int cm) { return sendCommandWithRetry("left " + std::to_string(constrain(cm, 20, 500))); }
bool TelloESP32::right(int cm) { return sendCommandWithRetry("right " + std::to_string(constrain(cm, 20, 500))); }
bool TelloESP32::forward(int cm) { return sendCommandWithRetry("forward " + std::to_string(constrain(cm, 20, 500))); }
bool TelloESP32::back(int cm) { return sendCommandWithRetry("back " + std::to_string(constrain(cm, 20, 500))); }
bool TelloESP32::rotateClockwise(int degrees) { return sendCommandWithRetry("cw " + std::to_string(constrain(degrees, 1, 360))); }
bool TelloESP32::rotateCounterClockwise(int degrees) { return sendCommandWithRetry("ccw " + std::to_string(constrain(degrees, 1, 360))); }
bool TelloESP32::flip(char direction)
{
    std::string command = "flip ";
    command += direction;
    return sendCommandWithRetry(command);
}

// Video settings commands
bool TelloESP32::setVideoBitrate(int bitrate)
{
    std::string command = "setbitrate " + std::to_string(bitrate);
    return sendCommandWithRetry(command);
}

bool TelloESP32::setVideoFPS(const std::string &fps)
{
    std::string command = "setfps " + fps;
    return sendCommandWithRetry(command);
}

bool TelloESP32::setVideoResolution(const std::string &resolution)
{
    std::string command = "setresolution " + resolution;
    return sendCommandWithRetry(command);
}

// Start video stream
bool TelloESP32::startVideoStream()
{
    if (sendCommandWithRetry("streamon"))
    {
        if (videoStreamTaskHandle == nullptr)
        { // Check if task already exists
            videoUdp.begin(videoPort);
            xTaskCreatePinnedToCore(
                TelloESP32::videoStreamTask,
                "videoStreamTask",
                16384, 
                this,
                1,
                &videoStreamTaskHandle,
                0);
        }
        return true;
    }
    return false;
}

// Stop video stream
bool TelloESP32::stopVideoStream()
{
    if (sendCommandWithRetry("streamoff"))
    {
        if (videoStreamTaskHandle != nullptr)
        {
            vTaskDelete(videoStreamTaskHandle);
            videoStreamTaskHandle = nullptr;
            delay(500);  // Delay for task cleanup
        }
        
        videoUdp.stop();
        delay(500);  // Delay for UDP cleanup
        return true;
    }
    return false;
}

// Set callback for video stream data
void TelloESP32::onVideoStreamData(std::function<void(const uint8_t *buffer, size_t size)> callback)
{
    videoStreamCallback = callback;
}

// Set callback for connection lost
void TelloESP32::onConnectionLost(std::function<void()> callback)
{
    connectionLostCallback = callback;
}

// Task to handle video stream data
void TelloESP32::videoStreamTask(void *pvParameters)
{
    TelloESP32 *tello = static_cast<TelloESP32 *>(pvParameters); // Explicitly cast void pointer
    while (true)
    {
        uint8_t buffer[5120]; 
        int packetSize = tello->videoUdp.parsePacket();
        if (packetSize)
        {
            int len = tello->videoUdp.read(buffer, packetSize); // Read up to packetSize bytes
            if (len > 0 && tello->videoStreamCallback)
            {
                tello->videoStreamCallback(buffer, len);
            }
        }
        vTaskDelay(1); // Yield to other tasks
    }
}

// Telemetry commands
String TelloESP32::getBattery()
{
    std::string response = sendCommandWithReturn("battery?");
    return String(response.c_str());
}

String TelloESP32::getSpeed()
{
    std::string response = sendCommandWithReturn("speed?");
    return String(response.c_str());
}

String TelloESP32::getTime()
{
    std::string response = sendCommandWithReturn("time?");
    return String(response.c_str());
}

String TelloESP32::getHeight()
{
    std::string response = sendCommandWithReturn("height?");
    return String(response.c_str());
}

String TelloESP32::getTemp()
{
    std::string response = sendCommandWithReturn("temp?");
    return String(response.c_str());
}

String TelloESP32::getAttitude()
{
    std::string response = sendCommandWithReturn("attitude?");
    return String(response.c_str());
}

String TelloESP32::getBarometer()
{
    std::string response = sendCommandWithReturn("baro?");
    return String(response.c_str());
}

String TelloESP32::getAcceleration()
{
    std::string response = sendCommandWithReturn("acceleration?");
    return String(response.c_str());
}

String TelloESP32::getTOF()
{
    std::string response = sendCommandWithReturn("tof?");
    return String(response.c_str());
}

String TelloESP32::getWifiSnr()
{
    std::string response = sendCommandWithReturn("wifi?");
    return String(response.c_str());
}

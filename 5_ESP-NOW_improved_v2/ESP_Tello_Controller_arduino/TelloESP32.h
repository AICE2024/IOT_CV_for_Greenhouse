#ifndef TELLOESP32_H
#define TELLOESP32_H

#include <WiFi.h>
#include <WiFiUdp.h>
#include <string>
#include <functional>

class TelloESP32
{
public:
    TelloESP32();

    bool connect(const char *ssid, const char *password, unsigned long timeout_ms = 10000);
    void disconnect();

    // Flight control commands
    bool takeoff();
    bool land();
    bool up(int cm);
    bool down(int cm);
    bool left(int cm);
    bool right(int cm);
    bool forward(int cm);
    bool back(int cm);
    bool rotateClockwise(int degrees);
    bool rotateCounterClockwise(int degrees);
    bool flip(char direction); // 'f' forward 'b' back 'l' left 'r' right

    // Video stream commands
    bool startVideoStream();
    bool stopVideoStream();

    // Video settings
    bool setVideoBitrate(int bitrate);
    bool setVideoFPS(const std::string &fps);
    bool setVideoResolution(const std::string &resolution);

    // Event handlers
    void onVideoStreamData(std::function<void(const uint8_t *buffer, size_t size)> callback);
    void onConnectionLost(std::function<void()> callback);

    // Telemetry commands
    String getBattery();
    String getSpeed();
    String getTime();
    String getHeight();
    String getTemp();
    String getAttitude();
    String getBarometer();
    String getAcceleration();
    String getTOF();
    String getWifiSnr();

    // Constants for video settings
    static const int BITRATE_AUTO = 0;
    static const int BITRATE_1MBPS = 1;
    static const int BITRATE_2MBPS = 2;
    static const int BITRATE_3MBPS = 3;
    static const int BITRATE_4MBPS = 4;
    static const int BITRATE_5MBPS = 5;
    static const std::string RESOLUTION_480P;
    static const std::string RESOLUTION_720P;
    static const std::string FPS_5;
    static const std::string FPS_15;
    static const std::string FPS_30;

private:
    WiFiUDP udp;
    WiFiUDP videoUdp;
    IPAddress telloAddr;
    uint16_t telloPort;
    uint16_t localPort;
    uint16_t videoPort;
    uint16_t commandTimeout;
    TaskHandle_t videoStreamTaskHandle;
    TaskHandle_t receiveResponseTaskHandle;
    TaskHandle_t connectionMonitorTaskHandle;
    SemaphoreHandle_t responseSemaphore;
    bool connected;
    std::string latestResponse;

    void sendCommand(const std::string &command);
    std::string sendCommandWithReturn(const std::string &command, int timeoutMs = 10000);
    bool sendCommandWithRetry(const std::string &command, const std::string &expectedResponse = "ok", int retries = 5, int delayMs = 1000, int timeoutMs = 10000);
    static void receiveResponseTask(void *pvParameters);
    static void videoStreamTask(void *pvParameters);
    static void connectionMonitorTask(void *pvParameters);

    std::function<void(const uint8_t *buffer, size_t size)> videoStreamCallback;
    std::function<void()> connectionLostCallback;
};

#endif // TELLOESP32_H
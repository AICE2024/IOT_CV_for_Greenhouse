#ifndef InferenceHandler_h
#define InferenceHandler_h

#include <WiFi.h> 
#include <WiFiClientSecure.h>
#include "FS.h"
#include "SD_MMC.h"
#include <ArduinoJson.h>

struct InferenceResult {
    float ripenessPercentage;
    int totalObjects;
    int ripeCount;
    int unripeCount;
    int greenCount;
    int frameCount;
};

class InferenceHandler {
public:
    InferenceHandler(const char* ssid, const char* password, const char* host, int httpsPort);
    bool begin();
    bool requestInference(const char* filename, float confidence, float overlap, InferenceResult& result);
    void end();

private:
    const int requestTimeout = 50000;
    const char* _ssid;
    const char* _password;
    const char* _host;
    const int _httpsPort;
    WiFiClientSecure _client;

    bool connectToServer();
    String makeMultipartRequest(File& file, const char* filename, float confidence, float overlap);
    float calculateRipenessPercentage(const JsonObject& predictions, int totalObjects);
};

#endif
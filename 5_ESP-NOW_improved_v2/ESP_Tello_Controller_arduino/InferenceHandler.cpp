#include "InferenceHandler.h"
#include <mbedtls/base64.h>

InferenceHandler::InferenceHandler(const char *ssid, const char *password, const char *host, int httpsPort) : _ssid(ssid), _password(password), _host(host), _httpsPort(httpsPort) {}

bool InferenceHandler::begin()
{
    // Connect to WiFi
    WiFi.begin(_ssid, _password);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(1000);
        Serial.print(".");
    }
    Serial.println("\nConnected to WiFi");

    // Initialize client settings
    _client.setInsecure();
    _client.setHandshakeTimeout(10);
    _client.setTimeout(requestTimeout);

    // Connect to server
    Serial.print("Connecting to server...");
    if (!connectToServer()) {
        Serial.println("Failed to connect to server during initialization");
        return false;
    }

    return true;
}

bool InferenceHandler::connectToServer()
{
    int attempts = 0;
    while (attempts < 3) {
        Serial.printf("Connection attempt %d...\n", attempts + 1);
        if (_client.connect(_host, _httpsPort)) {
            Serial.println("Connected to server successfully");
            return true;
        }
        attempts++;
        delay(1000);
    }
    
    Serial.println("Connection failed after 3 attempts!");
    return false;
}

String InferenceHandler::makeMultipartRequest(File &file, const char *filename, float confidence, float overlap)
{
    // Connection is maintained from begin()
    if (!_client.connected()) {
        Serial.println("Lost connection to server");
        return "";
    }

    // Clear any pending data
    while(_client.available()) {
        _client.read();
    }
    
    String boundary = "boundary123";
    String lineEnd = "\r\n";
    String twoHyphens = "--";

    // Calculate total content length
    size_t requestLength = 0;  

    // Confidence part
    String confidencePart = twoHyphens + boundary + lineEnd +
                            "Content-Disposition: form-data; name=\"confidence\"" + lineEnd + lineEnd +
                            String(confidence) + lineEnd;
    requestLength += confidencePart.length();

    // Overlap part
    String overlapPart = twoHyphens + boundary + lineEnd +
                         "Content-Disposition: form-data; name=\"overlap\"" + lineEnd + lineEnd +
                         String(overlap) + lineEnd;
    requestLength += overlapPart.length();

    // File part headers
    String fileHeader = twoHyphens + boundary + lineEnd +
                        "Content-Disposition: form-data; name=\"file\"; filename=\"" + String(filename) + "\"" + lineEnd +
                        "Content-Type: application/octet-stream" + lineEnd + lineEnd;
    requestLength += fileHeader.length();

    // File content
    requestLength += file.size();

    // Closing boundary
    String closing = lineEnd + twoHyphens + boundary + twoHyphens + lineEnd;
    requestLength += closing.length();

    // Request headers
    _client.print("POST /infer HTTP/1.1\r\n");
    _client.print("Host: ");
    _client.println(_host);
    _client.println("User-Agent: ESP32");
    _client.print("Content-Type: multipart/form-data; boundary=");
    _client.println(boundary);
    _client.print("Content-Length: ");
    _client.println(requestLength);
    _client.println("Connection: keep-alive"); 
    _client.println();

    // Send body
    _client.print(confidencePart);
    _client.print(overlapPart);
    _client.print(fileHeader);

    // Send file in chunks
    uint8_t buffer[1024];
    size_t bytesRead;
    while ((bytesRead = file.read(buffer, sizeof(buffer))) > 0)
    {
        _client.write(buffer, bytesRead);
    }

    _client.print(closing);

    unsigned long startTime = millis();
    String response;
    bool headersDone = false;
    size_t responseLength = 0;
    
    while (_client.connected() && (millis() - startTime < 10000)) { // 10 second timeout
        if (!headersDone) {
            String line = _client.readStringUntil('\n');
            if (line.startsWith("Content-Length: ")) {
                responseLength = line.substring(16).toInt();
            }
            if (line == "\r") {
                headersDone = true;
            }
            continue;
        }

        if (responseLength > 0 && _client.available()) {
            response.reserve(responseLength); 
            while (response.length() < responseLength && _client.available()) {
                response += (char)_client.read();
            }
            break;
        }
        delay(10); // Small delay to prevent tight loop
    }

    return response;
}

float InferenceHandler::calculateRipenessPercentage(const JsonObject &predictions, int totalObjects)
{
    if (totalObjects == 0)
        return 0.0f;

    int ripeCount = predictions["ripe"] | 0;
    return (float)ripeCount / totalObjects * 100.0f;
}

bool InferenceHandler::requestInference(const char *filename, float confidence, float overlap, InferenceResult &result)
{
    int retries = 3;
    while (retries > 0) {
        File file = SD_MMC.open(filename, FILE_READ);
        if (!file) {
            Serial.println("Failed to open file");
            return false;
        }

        String response = makeMultipartRequest(file, filename, confidence, overlap);
        file.close();

        if (response.length() > 0) {
            StaticJsonDocument<1024> doc; 
            DeserializationError error = deserializeJson(doc, response);
            if (error)
            {
                Serial.println("JSON parsing failed");
                return false;
            }

            result.frameCount = doc["frame_count"] | 0;
            result.totalObjects = doc["total_objects"] | 0;

            JsonObject predictions = doc["predictions"];
            result.ripeCount = predictions["ripe"] | 0;
            result.unripeCount = predictions["unripe"] | 0;
            result.greenCount = predictions["green"] | 0;

            result.ripenessPercentage = calculateRipenessPercentage(predictions, result.totalObjects);
            return true;
        }

        Serial.printf("Attempt %d failed, retrying...\n", 4-retries);
        retries--;
        delay(1000);
    }

    Serial.println("All retry attempts failed");
    return false;
}

void InferenceHandler::end()
{
    _client.stop();
    WiFi.disconnect(true);
    Serial.println("InferenceHandler: Cleaned up connections");
}
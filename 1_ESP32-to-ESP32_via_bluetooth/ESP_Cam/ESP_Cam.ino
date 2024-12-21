/*
 * =======================================================================================
 * Project: ESP32 Bluetooth & Camera Integration for Real-Time Image Transmission
 * Copyright (c) 2024 Oleg Shovkovyy
 * Date: 	  11-09-2024
 * Version: 1.0
 * Description: 
 * This project demonstrates the integration of Bluetooth and camera functionality using 
 * an ESP32 microcontroller to enable real-time image capture and transmission. The system 
 * is designed to capture photos, encode them in Base64 format, and transmit them over 
 * Bluetooth to a paired device. Additionally, it integrates a scheduling mechanism to 
 * automate operations at specific times and utilizes deep sleep mode to conserve power 
 * when the device is idle.
 *
 * Key Features:
 * - **Camera Integration**: Leverages the ESP32-CAM module to capture high-quality images 
 *   and encode them in Base64 for efficient transmission.
 * - **Bluetooth Communication**: Uses the ESP32's Bluetooth capabilities to establish a 
 *   master-slave connection for seamless data transfer.
 * - **Real-Time Scheduling**: Implements a time-based scheduler to trigger image capture 
 *   and transmission at pre-defined intervals (e.g., specific hours of the day).
 * - **Power Efficiency**: Utilizes the ESP32's deep sleep mode to minimize power 
 *   consumption during idle periods, making it ideal for battery-powered applications.
 * - **Error Handling**: Includes robust error handling for camera initialization, 
 *   Bluetooth connectivity, and data transmission to ensure system reliability.
 * - **Scalability**: The modular architecture allows for easy integration with additional 
 *   features, such as Wi-Fi connectivity or cloud-based image processing.
 * - **Security Concerns**: 
 *     - **Passwords**: Avoid hard-coding sensitive data like Wi-Fi credentials, passwords, 
 *       or API keys directly into the code. Instead, consider securely storing these values 
 *       in a configuration file or using environment variables during runtime. 
 *     - **API Keys**: When connecting to external services (e.g., cloud platforms, image 
 *       processing services), ensure that API keys are kept secure. These keys should be 
 *       stored in an encrypted format or fetched from a secure, remote location instead 
 *       of being hard-coded.
 *     - **Bluetooth Pairing**: Bluetooth credentials such as device names and MAC 
 *       addresses should be securely handled to avoid unauthorized access or spoofing. 
 *       A more secure pairing mechanism (e.g., PIN or authentication tokens) should be 
 *       considered for production environments.
 *
 * Technical Highlights:
 * - **Bluetooth Master Mode**: Configures the ESP32 as a Bluetooth master, enabling 
 *   direct communication with other Bluetooth devices.
 * - **Image Encoding**: Converts raw image data to Base64 format for compatibility with 
 *   most communication protocols and storage systems.
 * - **Custom Sleep Scheduler**: Dynamically calculates the duration until the next 
 *   scheduled task and configures the ESP32 to wake up accordingly.
 * - **URL Encoding**: Ensures compatibility of transmitted data with web-based platforms 
 *   by encoding special characters in URLs.

 * This project serves as a foundational example for integrating ESP32's camera and 
 * Bluetooth capabilities in real-world applications requiring low-power, automated, 
 * and scalable image capture and transmission.
 * =======================================================================================
 */

 /*
 * DISCLAIMER:
 * This code is shared for educational and demonstration purposes only.
 * Ensure you replace placeholder values with your own secure credentials before deploying.
 */
 
// ====================================== Includes ======================================
#include "esp_camera.h"
#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"
#include "BluetoothSerial.h"
#include "Base_64.h"

// ============================= MAC Address Configuration =============================
uint8_t SMA[6] = {0xCC, 0x7B, 0x5C, 0xA7, 0x0A, 0x81}; 
String myName = "ESP32-BT-Master";
BluetoothSerial SerialBT;

#define uS_TO_S_FACTOR 3600000000 // microseconds to hours conversion factor

// ======================================= Setup =======================================
void setup()
{
    Serial.begin(115200);

    // Initialize the camera
    initCamera();
    delay(500);

    // Initialize and connect Bluetooth
    BT_init();
    BT_connect();
}

// ============================== Bluetooth Initialization ==============================
void BT_init()
{
    if (!SerialBT.begin(myName, true))
    {
        Serial.println("An error occurred initializing Bluetooth");
        ESP.restart();
    }
    else
    {
        Serial.println("Bluetooth initialized");
    }
    SerialBT.register_callback(btCallback);
}

// =============================== Bluetooth Connection ================================
void BT_connect()
{
    bool connected;
    connected = SerialBT.connect(SMA);
    if (connected)
    {
        Serial.println("Connected Successfully!");
    }
    else
    {
        Serial.println("Can't Connect!");
        ESP.restart();
    }
}

// ============================ Bluetooth Event Callback ================================
void btCallback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
    if (event == ESP_SPP_SRV_OPEN_EVT)
    {
        Serial.println("Connected!");
    }
    else if (event == ESP_SPP_DATA_IND_EVT)
    {
        String stringRead = String(*param->data_ind.data);
        int hour = stringRead.toInt() - 48;
        send_image_robot_flow(hour);
        calculate_time_sleep(hour);
    }
}

// ============================== Base64 Photo Encoding ===============================
String Photo2Base64()
{
    camera_fb_t *fb = NULL;
    fb = esp_camera_fb_get();
    if (!fb)
    {
        Serial.println("Camera capture failed");
        return "";
    }
    String imageFile = "";
    char *input = (char *)fb->buf;
    char output[base_64_enc_len(3)];
    for (int i = 0; i < fb->len; i++)
    {
        base_64_encode(output, (input++), 3);
        if (i % 3 == 0)
            imageFile += urlencode(String(output));
    }
    esp_camera_fb_return(fb);
    esp_camera_deinit();
    return imageFile;
}

// ================================ URL Encoding ==================================
String urlencode(String str)
{
    const char *msg = str.c_str();
    const char *hex = "0123456789ABCDEF";
    String encodedMsg = "";
    while (*msg != '\0')
    {
        if (('a' <= *msg && *msg <= 'z') || ('A' <= *msg && *msg <= 'Z') || ('0' <= *msg && *msg <= '9') || *msg == '-' || *msg == '_' || *msg == '.' || *msg == '~')
        {
            encodedMsg += *msg;
        }
        else
        {
            encodedMsg += '%';
            encodedMsg += hex[(unsigned char)*msg >> 4];
            encodedMsg += hex[*msg & 0xf];
        }
        msg++;
    }
    return encodedMsg;
}

// ================================== Image Capture ===================================
void capture()
{
    Serial.println("Encode base64");
    String encodedString = Photo2Base64();
    Serial.println("Send image base64 by Bluetooth");
    SerialBT.println(encodedString);
    SerialBT.flush();
}

// ======================= Image Transmission to Robotflow ========================
void send_image_robot_flow(int time_now)
{
    if (time_now == 6 || time_now == 11 || time_now == 16)
    {
        initCamera();
        delay(500);

        capture();
        delay(1000);
    }
    else
    {
        Serial.println("It's not time to work yet");
    }
    delay(100);
}

// ========================== Calculate Sleep Duration ============================
void calculate_time_sleep(int time_now)
{
    int time_sleep = 0;
    if (time_now < 6)
    {
        time_sleep = 6 - time_now;
    }
    else if (time_now < 11)
    {
        time_sleep = 11 - time_now;
    }
    else if (time_now < 16)
    {
        time_sleep = 16 - time_now;
    }
    else
    {
        time_sleep = 6 + 24 - time_now;
    }
    esp_sleep_enable_timer_wakeup(time_sleep * uS_TO_S_FACTOR);
    delay(100);
    esp_deep_sleep_start();
}

// ================================= Initialize Camera ===============================
void initCamera()
{
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

    if (psramFound())
    {
        config.frame_size = FRAMESIZE_UXGA;
        config.jpeg_quality = 10;
        config.fb_count = 2;
    }
    else
    {
        config.frame_size = FRAMESIZE_SVGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK)
    {
        Serial.printf("Camera init failed with error 0x%x", err);
        ESP.restart();
    }
}

// ======================================= Loop ========================================
void loop() {}

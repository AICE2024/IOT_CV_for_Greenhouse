#ifndef COMMON_H // Unique identifier for the header guard

#define COMMON_H

#include "FS.h"                // ESP32 SD card
#include "SD_MMC.h"            // ESP32 SD card
#include "soc/soc.h"           // Disable brownout detector
#include "soc/rtc_cntl_reg.h"  // Disable brownout detector
#include "driver/rtc_io.h" 
#include <esp_sleep.h>
#include "WiFiUdp.h"
#include "WiFi.h"
#include "HTTPClient.h"
#include "ArduinoJson.h"
#include "esp_camera.h"
#include <esp_now.h>
#include "esp_wifi.h"
#endif // COMMON_H

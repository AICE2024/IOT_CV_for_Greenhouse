#ifndef CAMFUNCTIONS_H
#define CAMFUNCTIONS_H

#include "esp_camera.h"
#include "SD_MMC.h"
#include "FS.h"

#ifndef CAMERA_PINS_H
#define CAMERA_PINS_H

#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22
#endif

// Callback type definition for image capture
typedef void (*ImageCaptureCallback)(camera_fb_t* fb);

bool startContinuousCapture(unsigned long intervalMs, ImageCaptureCallback callback);
void stopContinuousCapture();

// Task control variables
extern TaskHandle_t cameraTaskHandle;
extern bool isCaptureRunning;
extern unsigned long lastCaptureTime;
extern unsigned long captureInterval;
extern ImageCaptureCallback imageCaptureCallback;

bool initCamera();
bool captureAndSaveImage(const String &path);
bool deinitCamera();  

#endif
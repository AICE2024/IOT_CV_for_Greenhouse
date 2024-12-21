# IOT_CV_for_Greenhouse
In this project, I explore the feasibility of using computer vision, utilizing images captured from a fixed camera and a drone's camera, 
to detect the presence of ripe tomatoes in a greenhouse. This data serves as input for the greenhouse's automated watering system. 
The project employs an ESP32-WiFi microcontroller to build the watering automation system and an ESP32-CAM module to: 
(1) capture images with its camera, 
(2) operate a TELLO drone, and 
(3) process images and videos recorded by the ESP32-CAM and drone. 
The processing involves storing the data on an SD card, sending it to Roboflow for further analysis using a pretrained YOLOv8 model, 
and interpreting the JSON response from Roboflow to determine the percentage of ripe tomatoes in the greenhouse.

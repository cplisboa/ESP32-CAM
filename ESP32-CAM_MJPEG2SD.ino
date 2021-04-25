
#include "esp_camera.h"
#include <WiFi.h>

// current arduino-esp32 stable release is v1.0.4
#define USE_v104 // comment out to use with arduino-esp32 development release v1.0.5-rc4

//
// WARNING!!! Make sure that you have either selected ESP32 Wrover Module,
//            or another board which has PSRAM enabled
//

// Select camera model
//#define CAMERA_MODEL_WROVER_KIT
//#define CAMERA_MODEL_ESP_EYE
//#define CAMERA_MODEL_M5STACK_PSRAM
//#define CAMERA_MODEL_M5STACK_WIDE
#define CAMERA_MODEL_AI_THINKER

#include "camera_pins.h"
#include "myConfig.h"

void startCameraServer();
bool prepMjpeg();
void startSDtasks();
bool prepSD_MMC();
bool prepDS18();
void OTAsetup();
bool OTAlistener();
bool startWifi();
void checkConnection();                         

const char* appVersion = "2.0";

// fastest clock rate is changed for release v1.0.5
#ifdef USE_v104
#define XCLK_MHZ 10
#else
#define XCLK_MHZ 20
#endif

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

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
  config.xclk_freq_hz = XCLK_MHZ*1000000;
  config.pixel_format = PIXFORMAT_JPEG;
  //init with high specs to pre-allocate larger buffers
  if (psramFound()){
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 4;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  // camera init
  esp_err_t err = ESP_FAIL;
  uint8_t retries = 2;
  while (retries && err != ESP_OK) {
    err = esp_camera_init(&config);
    if (err != ESP_OK) {
      Serial.printf("Camera init failed with error 0x%x", err);
      digitalWrite(PWDN_GPIO_NUM, 1);
      delay(100);
      digitalWrite(PWDN_GPIO_NUM, 0); // power cycle the camera (OV2640)
      retries--;
    }
  } 
  if (err != ESP_OK) ESP.restart();
  else Serial.println("Camera init OK");

  sensor_t * s = esp_camera_sensor_get();
  //initial sensors are flipped vertically and colors are a bit saturated
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);//flip it back
    s->set_brightness(s, 1);//up the brightness just a bit
    s->set_saturation(s, -2);//lower the saturation
  }
  //drop down frame size for higher initial frame rate
  s->set_framesize(s, FRAMESIZE_SVGA);

#if defined(CAMERA_MODEL_M5STACK_WIDE)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif

  prepSD_MMC();
  
  //Connect wifi and start config AP if fail
  if(!startWifi()){
    Serial.println("Failed to start wifi, restart after 10 secs");
    delay(10000);
    ESP.restart();
  }
  
  if (!prepMjpeg()) {
    Serial.println("Unable to continue, SD card fail, restart after 10 secs");
    delay(10000);
    ESP.restart();
  }
  //Start httpd
  startCameraServer();
  OTAsetup();
  startSDtasks();
  if (prepDS18()) Serial.println("DS18B20 device available");
  else Serial.println("DS18B20 device not present"); 
  
  String wifiIP = (WiFi.getMode() == WIFI_AP) ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  Serial.printf("Free DRAM: %u, free pSRAM %u\n", ESP.getFreeHeap(), ESP.getFreePsram());
  Serial.printf("Camera Ready @ %uMHz, version %s. Use 'http://%s' to connect\n", XCLK_MHZ, appVersion, wifiIP.c_str());
}

void loop() {
  //Check connection
  checkConnection();                    
  if (!OTAlistener()) delay(100000);
}

uint8_t fsizeLookup(uint8_t lookup, bool old2new) {
  // framesize enum in v1.0.4 not compatible with v1.0.5
#ifdef USE_v104
  // unmapped lookups default to nearest res
  static uint8_t oldNewData[] = {1,1,1,3,5,6,8,9,10,12,13}; 
  static uint8_t newOldData[] = {FRAMESIZE_QQVGA,FRAMESIZE_QQVGA,FRAMESIZE_HQVGA,FRAMESIZE_HQVGA,FRAMESIZE_QVGA,FRAMESIZE_QVGA,FRAMESIZE_CIF,FRAMESIZE_VGA,FRAMESIZE_VGA,FRAMESIZE_SVGA,FRAMESIZE_XGA,FRAMESIZE_SXGA,FRAMESIZE_SXGA,FRAMESIZE_UXGA};
  lookup = (old2new) ? oldNewData[lookup] : newOldData[lookup]; // read : set
#endif
  return lookup;
}

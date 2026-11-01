/*
 * RIRAOSAN
 *
 * Licensed under the MIT License
 */
#include <Arduino.h>
#include <WiFi.h>
#include <StackChan-BSP.h>
#include <Avatar.h>
#include <AudioFileSourceHTTPStream.h>
#include <AudioFileSourceBuffer.h>
#include <AudioGeneratorMP3.h>
#include "AudioOutputM5Speaker.h"

static AudioOutputM5Speaker out(&M5.Speaker, 0);
static AudioFileSourceHTTPStream *file = nullptr;
static AudioFileSourceBuffer *buff = nullptr;
static AudioGeneratorMP3 *mp3 = nullptr;

static const char *STREAM_URL = "http://listen.181fm.com/181-beatles_128k.mp3";

// ログ確認用
void StatusCallback(void *cbData, int code, const char *string) {
  Serial.printf("status(%d): %s\n", code, string);
}
void MDCallback(void *cbData, const char *type, bool isUnicode, const char *string) {
  Serial.printf("meta: %s = %s\n", type, string);
}

#define SD_CS          4
#define SD_MOSI       37
#define SD_MISO       35
#define SD_SCK        36
#define I2S_DOUT      13
#define I2S_BCLK      34
#define I2S_LRC       33

using namespace m5avatar;

static const char *TAG = "MAIN";

//for 2.4GHz WiFi
String ssid =     "xxxx";
String password = "xxxx";

/**
 * @brief
 * Angle unit: 10 = 1 degrees, Speed range: 0~1000
 * Range X: -1280 ~ 1280 (-128° ~ 128°), Range Y: 0 ~ 900 (0° ~ 90°)
 *
 */
struct Keyframe_t {
    int x             = 0;
    int y             = 0;
    int speed         = 500;
    uint32_t interval = 0;
};

/* Move around */
std::vector<Keyframe_t> dance_1 = {
    {0, 0, 500, 1000},      // Home, 1s
    {600, 200, 800, 500},   // Left, fast
    {-600, 200, 800, 500},  // Right, fast
    {600, 200, 800, 500},   // Left, fast
    {-600, 200, 800, 500},  // Right, fast
    {0, 800, 900, 400},     // Look Up
    {0, 0, 900, 400},       // Look Down
    {0, 800, 900, 400},     // Look Up
    {0, 0, 900, 400},       // Look Down
    {800, 700, 700, 500},   // Top Left
    {-800, 700, 700, 500},  // Top Right
    {800, 700, 700, 500},   // Top Left
    {-800, 700, 700, 500},  // Top Right
    {0, 0, 500, 1000}       // Back Home
};

/* Shake */
std::vector<Keyframe_t> dance_2 = {
    {0, 0, 500, 1000},    // Home
    {300, 0, 750, 250},   // Shake Left
    {-300, 0, 750, 250},  // Shake Right
    {300, 0, 750, 250},   // Shake Left
    {-300, 0, 750, 250},  // Shake Right
    {300, 0, 750, 250},   // Shake Left
    {-300, 0, 750, 250},  // Shake Right
    {0, 0, 500, 1000}     // Back Home
};

/* Nod */
std::vector<Keyframe_t> dance_3 = {
    {0, 0, 500, 1000},   // Home
    {0, 350, 900, 250},  // Nod Up
    {0, 0, 900, 250},    // Nod Down
    {0, 350, 900, 250},  // Nod Up
    {0, 0, 900, 250},    // Nod Down
    {0, 350, 900, 250},  // Nod Up
    {0, 0, 900, 250},    // Nod Down
    {0, 0, 500, 500}     // Back Home
};

std::vector<std::vector<Keyframe_t>*> dances = {
    //&dance_1,
    &dance_2,
    &dance_3,
};

static int dance_index = 0;

static int flag = 0;

Avatar avatar;

bool g_isServoTaskRunning = false;

static void audioTask(void *) {
  for (;;) {
    if (mp3 && mp3->isRunning()) {
      if (!mp3->loop()) {
        mp3->stop();
        Serial.println("MP3 stopped");
      }
    }
    vTaskDelay(1);
  }
}

static void startRadio() {
  file = new AudioFileSourceHTTPStream(STREAM_URL);
  file->RegisterStatusCB(StatusCallback, nullptr);

  buff = new AudioFileSourceBuffer(file, 65536);
  buff->RegisterStatusCB(StatusCallback, nullptr);

  mp3 = new AudioGeneratorMP3();
  mp3->RegisterStatusCB(StatusCallback, nullptr);
  mp3->begin(buff, &out);
}

void servo(void *args)
{
  float gazeX, gazeY;
  DriveContext *ctx = (DriveContext *)args;
  Avatar *avatar = ctx->getAvatar();

  for (;;)
  {
    if(g_isServoTaskRunning) {
      avatar->getGaze(&gazeY, &gazeX);

      M5StackChan.Motion.move(90 + (int)(120.0 * gazeX), 90 + (int)(120.0 * gazeY) , 500);
      delay(1000);

      if(flag == 1){
        M5StackChan.Motion.move(90, 90, 500);
        delay(500);
        flag = 0;
      }
    }
    delay(50);
  }
}

void lipSync(void *args)
{
  float gazeX, gazeY;
  int level = 0;
  DriveContext *ctx = (DriveContext *)args;
  Avatar *avatar = ctx->getAvatar();

  for (;;){
    level = abs(*out.getBuffer());
    if(level<1500) level = 0;
    if(level > 15000)
    {
      level = 15000;
    }
    float open = (float)level/15000.0;
    avatar->setMouthOpenRatio(open);
    avatar->getGaze(&gazeY, &gazeX);
    avatar->setRotation(gazeX * 5);

    delay(50);
  }
}

void setup()
{
  Serial.begin(115200);
  M5StackChan.begin();

  M5.Speaker.setVolume(100);   // 0〜255

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(ssid.c_str(), password.c_str());
  while (!WiFi.isConnected()) delay(10);
  Serial.println("WiFi connected");

  startRadio();
  xTaskCreatePinnedToCore(audioTask, "audio", 16384, nullptr, 5, nullptr, 0);

  avatar.init();
  avatar.addTask(servo, "servo", 4096);
  avatar.addTask(lipSync, "lipsync");
  M5StackChan.Motion.setAutoAngleSyncEnabled(false);
}

void loop()
{
    M5StackChan.update();
    if (M5StackChan.TouchSensor.wasPressed()) {
        g_isServoTaskRunning = false;
        delay(200);
        log_i("enter1");

        for (const auto& frame : *dances[dance_index]) {
            M5StackChan.Motion.move(frame.x, frame.y, frame.speed);
            delay(frame.interval);
        }

        dance_index = (dance_index + 1) % dances.size();
    }

    auto t = M5.Touch.getDetail();

    if(t.wasPressed()){
        log_i("enter2");
        flag = 1;
    }

    g_isServoTaskRunning = true;

    delay(1);
}

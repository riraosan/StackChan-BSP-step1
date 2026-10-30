/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <Arduino.h>
#include <StackChan-BSP.h>
#include <Avatar.h>
#include "Audio.h"

#define SD_CS          4
#define SD_MOSI       37
#define SD_MISO       35
#define SD_SCK        36
#define I2S_DOUT      13
#define I2S_BCLK      34
#define I2S_LRC       33

using namespace m5avatar;

static const char *TAG = "MAIN";

Audio audio;
String ssid =     "OSAKA-BASE";
String password = "WCGy4YAw";

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

void setup()
{
   // Audio::audio_info_callback = my_audio_info;

    M5StackChan.begin();

    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI);
    SPI.setFrequency(1000000);
    SD.begin(SD_CS);

    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio.setVolume(15); // 0...21

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());
    while (!WiFi.isConnected()) {
      delay(10);
    }
    ESP_LOGI(TAG, "Connected");
    ESP_LOGI(TAG, "Starting MP3...\n");

//  audio.connecttoFS(SD, "/320k_test.mp3");
//  audio.connecttoFS(SD, "test.wav");
    audio.connecttohost("http://air.ofr.fm:8008/jazz/mp3/128");

    /* Init StackChan */
    avatar.init();
    avatar.addTask(servo, "servo", 4096);

    // Disable auto angle sync for smooth and continuous movement
    M5StackChan.Motion.setAutoAngleSyncEnabled(false);
}

void loop()
{
    M5StackChan.update();
    audio.loop();

    if (M5StackChan.TouchSensor.wasPressed()) {
        g_isServoTaskRunning = false;
        delay(200);
        log_i("enter1");
        /* Perform dance */
        //M5StackChan.Display().printf("> Dance #%d!\n", dance_index + 1);
        for (const auto& frame : *dances[dance_index]) {
            M5StackChan.Motion.move(frame.x, frame.y, frame.speed);
            delay(frame.interval);
        }
        //M5StackChan.Display().printf("> Finished!\n");

        /* Next dance */
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

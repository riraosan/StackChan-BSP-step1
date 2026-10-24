/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <Arduino.h>
#include <StackChan-BSP.h>
#include <Avatar.h>

using namespace m5avatar;

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
    &dance_1,
    &dance_2,
    &dance_3,
};

static int dance_index = 0;

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

      if(gazeY < 0) {
        int tmp = (int)(15.0 * gazeY);
        if(tmp > 15) tmp = 15;
        M5StackChan.Motion.move(90 + (int)(15.0 * gazeX), 90 + tmp, 500);
        delay(1000);
      } else {
        M5StackChan.Motion.move(90 + (int)(15.0 * gazeX), 90 + (int)(15.0 * gazeY) , 500);
        delay(1000);
      }
    }
    delay(50);
  }
}

void setup()
{
    /* Init StackChan */
    M5StackChan.begin();
    avatar.init();
    avatar.addTask(servo, "servo");

    // Disable auto angle sync for smooth and continuous movement
    M5StackChan.Motion.setAutoAngleSyncEnabled(false);
}

void loop()
{
    M5StackChan.update();
    if (M5StackChan.TouchSensor.wasPressed()) {
        g_isServoTaskRunning = false;
        delay(200);

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

    g_isServoTaskRunning = true;

    delay(100);
}

/*
 * RIRAOSAN
 *
 * Licensed under the MIT License
 */
#include <Arduino.h>
#include <WiFi.h>
#include <StackChan-BSP.h>
#include <Avatar.h>
//#include <AudioFileSourceHTTPStream.h>
//#include <AudioFileSourceBuffer.h>
//#include <AudioGeneratorMP3.h>
//#include "AudioOutputM5Speaker.h"
#include <SanoTTS.h>
#include <SanoTTSSpeakerM5.h>

//static AudioOutputM5Speaker out(&M5.Speaker, 0);
//static AudioFileSourceHTTPStream *file = nullptr;
//static AudioFileSourceBuffer *buff = nullptr;
//static AudioGeneratorMP3 *mp3 = nullptr;

static const char *STREAM_URL = "http://livestreaming-node-2.srg-ssr.ch/srgssr/rsp/mp3/128";

// ログ確認用
//void StatusCallback(void *cbData, int code, const char *string) {
//  log_i("status(%d): %s\n", code, string);
//}
//void MDCallback(void *cbData, const char *type, bool isUnicode, const char *string) {
//  log_i("meta: %s = %s\n", type, string);
//}

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
String ssid =     "俺のiPhone";
String password = "room03601";

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

static const float kMouthRefRms = 2500.0f;

class LipSyncSpeaker : public SanoTTSSpeakerM5 {
public:
  bool beginUtterance(size_t n) override {
    m_playing = false;
    m_head = m_tail = 0;
    m_written = 0;
    return SanoTTSSpeakerM5::beginUtterance(n);
  }
  bool prerollPush(const int16_t* pcm, size_t n) override {
    push(pcm, n);
    return SanoTTSSpeakerM5::prerollPush(pcm, n);
  }
  bool start() override {
    m_t0 = millis();
    m_playing = true;
    return SanoTTSSpeakerM5::start();
  }
  bool write(const int16_t* pcm, size_t n) override {
    push(pcm, n);                       /* write() はキューが空くまでブロックするので先に記録 */
    return SanoTTSSpeakerM5::write(pcm, n);
  }
  void stop() override {
    SanoTTSSpeakerM5::stop();           /* 鳴り終わるまで待ってから */
    m_playing = false;
  }

  /* 今この瞬間に鳴っているチャンクの口の開き具合 (0..1)。loop() から呼ぶ。 */
  float mouthOpenRatio() {
    if (!m_playing) return 0.0f;
    const uint32_t pos = (uint32_t)((uint64_t)(millis() - m_t0) * SanoTTS::kSampleRate / 1000);
    while (m_head != m_tail && m_ring[m_head].end <= pos) m_head = (m_head + 1) % kRing;
    if (m_head == m_tail || m_ring[m_head].start > pos) return 0.0f;
    return m_ring[m_head].level;
  }

private:
  struct Entry { uint32_t start, end; float level; };
  /* 書き手（TTS タスク）は再生より数チャンク先行するだけなので 32 で足りる */
  static const size_t kRing = 32;

  void push(const int16_t* pcm, size_t n) {
    uint64_t sq = 0;
    for (size_t i = 0; i < n; ++i) sq += (int32_t)pcm[i] * (int32_t)pcm[i];
    const float rms = n ? sqrtf((float)sq / (float)n) : 0.0f;
    float level = rms / kMouthRefRms;
    if (level > 1.0f) level = 1.0f;

    const size_t next = (m_tail + 1) % kRing;
    if (next == m_head) return;         /* 満杯なら捨てる（起きない想定） */
    m_ring[m_tail] = { m_written, (uint32_t)(m_written + n), level };
    m_written += n;
    m_tail = next;
  }

  Entry m_ring[kRing];
  volatile size_t m_head = 0, m_tail = 0;   /* 書き手 1 / 読み手 1 */
  uint32_t m_written = 0;
  uint32_t m_t0 = 0;
  volatile bool m_playing = false;
};

SanoTTS tts;
LipSyncSpeaker spk;
Avatar avatar;

static const char* kLines[] = {
  "こんにちは。私はスタックチャンです。",
  "今日は良い天気ですね。",
  "電池の残量は八十五パーセントです。",
  "画面をタッチすると、次の文を喋ります。",
};
static const size_t kNumLines = sizeof(kLines) / sizeof(kLines[0]);
static const char* kKanaLine = "きょ][おわよ][いて][んきです°ね";

static QueueHandle_t g_queue;           /* const char* を積む */

static void ttsTask(void*) {
  const char* text;
  for (;;) {
    if (xQueueReceive(g_queue, &text, portMAX_DELAY) != pdTRUE) continue;
    avatar.setSpeechText(text);
    if (!tts.say(text)) Serial.println(tts.lastError());
    else Serial.printf("%s (%u samples, absmax %d)\n", text,
                       (unsigned)tts.samples(), (int)tts.absmax());
    avatar.setSpeechText("");
  }
}

static void say(const char* text) {
  xQueueSend(g_queue, &text, 0);        /* 喋っている最中なら後回し（最大 4 件） */
}

static int dance_index = 0;

static int flag = 0;

bool g_isServoTaskRunning = false;
/*
static void audioTask(void *) {
  for (;;) {
    if (mp3 && mp3->isRunning()) {
      if (!mp3->loop()) {
        mp3->stop();
        log_i("MP3 stopped");
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
*/

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
    delay(1);
  }
}

/*
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

    delay(1);
  }
}
*/

void setup()
{
  log_i("Setup");
  M5StackChan.begin();
  M5.Speaker.setVolume(150);   // 0〜255

  avatar.setSpeechFont(&fonts::efontJA_16);   /* 吹き出しを日本語で */
  avatar.init();
  avatar.addTask(servo, "servo", 4096);
  //avatar.addTask(lipSync, "lipsync");
  M5StackChan.Motion.setAutoAngleSyncEnabled(false);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(ssid.c_str(), password.c_str());
  while (!WiFi.isConnected()) delay(10);
  log_i("WiFi connected");

  tts.setSpeaker(&spk);
  if (!tts.begin()) { Serial.println(tts.lastError()); return; }
  Serial.printf("kanji: %s\n", tts.kanjiReady() ? "ready" : "NOT ready (dict missing)");

  g_queue = xQueueCreate(4, sizeof(const char*));
  xTaskCreatePinnedToCore(ttsTask, "tts", 16384, nullptr, 2, nullptr, PRO_CPU_NUM);

  say(tts.kanjiReady() ? kLines[0] : kKanaLine);

  //startRadio();
  //xTaskCreatePinnedToCore(audioTask, "audio", 16384, nullptr, 5, nullptr, 0);

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

    static size_t idx = 1;
    auto t = M5.Touch.getDetail();

    if(t.wasPressed()){
        say(tts.kanjiReady() ? kLines[idx] : kKanaLine);
        idx = (idx + 1) % kNumLines;
        flag = 1;
    }

    g_isServoTaskRunning = true;

    delay(1);
}

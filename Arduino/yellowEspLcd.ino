// E32R28T 2.8" Touch Display 
// Display Driver ILI9341

#include <TFT_eSPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "User_Setup.h"
#include "Weyland_Yutani_Logo.h"
#include "radar_clean_full.h"

#define AUDIO_ENABLE 4
#define AUDIO_DAC 26
int centerX = 160;
int centerY = 155;
#define MAX_DISTANCE 25.0
#define TFT_BL 21
// #define SD_CS 5
TFT_eSPI tft = TFT_eSPI();
float simDistance;
float simAngle;
float simSpeed;
unsigned long startTime;
bool simStarted = false;
QueueHandle_t audioQueue;

enum AudioCue : uint8_t {
  AUDIO_CUE_BLIP,
  AUDIO_CUE_TARGET
};

// ----------------------------------------------
// Audio Functions
// ----------------------------------------------
void playTone(float frequency, unsigned long durationMs, int amplitude, bool fadeOut) {
  const unsigned int sampleRate = 8000;
  const unsigned long samples = (sampleRate * durationMs) / 1000;
  digitalWrite(AUDIO_ENABLE, LOW);

  for (unsigned long i = 0; i < samples; i++) {
    float time = (float)i / sampleRate;
    float envelope = fadeOut ? 1.0f - ((float)i / samples) : 1.0f;
    float wave = sinf(2.0f * PI * frequency * time);
    int output = 128 + (int)(wave * envelope * amplitude);
    dacWrite(AUDIO_DAC, output);
    delayMicroseconds(125);
  }

  dacWrite(AUDIO_DAC, 128);
  digitalWrite(AUDIO_ENABLE, HIGH);
}

void playRadarTick() {
  float proximity = 1.0f - (simDistance / MAX_DISTANCE);
  if (proximity < 0.0f) proximity = 0.0f;
  if (proximity > 1.0f) proximity = 1.0f;
  float frequency = 660.0f + (proximity * 120.0f);
  playTone(frequency, 220, 100, true);
}

void playRadarCombined() {
  playTone(300.0f, 150, 20, false);
  playTone(1438.0f, 500, 20, true);
}

void playRadarBeep() {
  playTone(1438.0f, 1300, 20, true);
}

void playRadarBlip() {
  playTone(300.0f, 150, 20, false);
}

void audioPlaybackTask(void *parameter) {
  AudioCue cue;
  for (;;) {
    if (xQueueReceive(audioQueue, &cue, portMAX_DELAY) == pdPASS) {
      if (cue == AUDIO_CUE_TARGET) {
        playRadarCombined();
      } else {
        playRadarBlip();
      }
      vTaskDelay(1);
    }
  }
}

void queueRadarAudio(AudioCue cue) {
  if (audioQueue == NULL) {
    return;
  }
  if (xQueueOverwrite(audioQueue, &cue) != pdPASS) {
    Serial.println("Failed to queue radar audio");
  }
}

// ----------------------------------------------
// Display Functions
// ----------------------------------------------
void headerImage() {
  tft.setSwapBytes(true);
  tft.pushImage(0, 0, radar_clean_full_width, radar_clean_full_height, radar_clean_full);
}

void logoImage() {
  tft.setSwapBytes(true);
  tft.pushImage(0, 0, Weyland_Yutani_Logo_width, Weyland_Yutani_Logo_height, Weyland_Yutani_Logo);
}

void startupDisplay() {
  const int barX = 45;
  const int barY = 195;
  const int barWidth = 230;
  const int barHeight = 16;
  const unsigned long duration = 4000;
  unsigned long startTime = millis();
  while (millis() - startTime < duration) {
    logoImage();
    tft.drawRect(barX, barY, barWidth, barHeight, TFT_WHITE);
    unsigned long elapsed = millis() - startTime;
    int progress = (elapsed * 100) / duration;
    int fillWidth = ((barWidth - 4) * progress) / 100;
    tft.fillRect(barX + 2, barY + 2, barWidth - 4, barHeight - 4, TFT_BLACK);
    if (fillWidth > 0) {
      tft.fillRect(barX + 2, barY + 2, fillWidth, barHeight - 4, TFT_WHITE);
    }
  }
  tft.fillRect(barX + 2, barY + 2, barWidth - 4, barHeight - 4, TFT_WHITE);
}

void displayDistance(float distance, float angle) {
  float radius = (distance / MAX_DISTANCE) * centerY;
  int x = centerX - radius * sin(angle);
  int y = centerY - radius * cos(angle);

  if (x < 0) x = 0;
  if (x > 319) x = 319;
  if (y < 0) y = 0;
  if (y > centerY) y = centerY;

  tft.fillCircle(x, y, 9, TFT_WHITE);
}

void displayDistanceText(float distance) {
  int xStart = 128;
  int yStart = 165;
  tft.setTextColor(TFT_RED);
  tft.setTextDatum(TL_DATUM);
  tft.setFreeFont(&FreeSans12pt7b);
  tft.setTextSize(1);
  tft.drawNumber((int)distance, xStart, yStart);
  xStart += 25;
  tft.setTextFont(1);
  tft.setTextSize(1);
  int length = tft.drawString(" - ", xStart, yStart+8);
  length = length + (tft.drawNumber((int)(distance * 10) % 10, xStart+length-1, yStart+3, 1) / 2);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("m", xStart+length, yStart+15); 
}

void displayDataText(int contacts, float averageDistance) {
  char data[32];
  int x = 220;
  int y = 173;
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(1);
  tft.setTextSize(1);
  sprintf(data, "C.X. %d/%.2f", contacts, averageDistance);
  tft.drawString(data, x, y);
  tft.setTextDatum(TR_DATUM);
  x = 100;
  tft.drawString("F.E.M.S. 9.47.2", x, y);
}

void radarSweep() {
  const unsigned long duration = 15000;
  int r = 15;
  bool targetPresent = simStarted || (millis() - startTime > duration);
  simStarted = targetPresent;
  queueRadarAudio(targetPresent ? AUDIO_CUE_TARGET : AUDIO_CUE_BLIP);

  while (r < 270) {
    headerImage();
    tft.drawArc(160, 164, r + 5, r, 90, 270, TFT_WHITE, TFT_BLACK, true);

    // SIMULATION
    float smallest = MAX_DISTANCE;
    float average = 0.0;

    if (targetPresent) {
      // Move target toward the center
      simDistance -= simSpeed * 0.01;

      // Reset target when it reaches the center
      if (simDistance <= 0.0) {
        simDistance = random(150, 251) / 10.0;
        simAngle = random(-90, 90) * DEG_TO_RAD;
        simSpeed = random(1, 5);

      }

      displayDistance(simDistance, simAngle);

      smallest = simDistance;
      average = simDistance;

      displayDataText(1, average);
      displayDistanceText(smallest); 

    } else {
      displayDataText(0, 0.0);
      displayDistanceText(0.0);
    }

    r += 30;
    delay(25);
  }
}

void setup() {
  Serial.begin(115200);

  audioQueue = xQueueCreate(1, sizeof(AudioCue));
  if (audioQueue == NULL) {
    Serial.println("Failed to create radar audio queue");
  } else if (xTaskCreatePinnedToCore(audioPlaybackTask, "radarAudio", 4096, NULL, 1, NULL, 0) != pdPASS) {
    Serial.println("Failed to start radar audio task");
    vQueueDelete(audioQueue);
    audioQueue = NULL;
  }

  // Audio setup
  pinMode(AUDIO_ENABLE, OUTPUT);
  digitalWrite(AUDIO_ENABLE, HIGH);
  dacWrite(AUDIO_DAC, 128);

  // Simulation setup
  simDistance = random(150, 251) / 10.0;
  simAngle = random(-90, 90) * DEG_TO_RAD;
  simSpeed = random(1, 5);

  // Backlight setup
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH); // Turn backlight ON

  // Display init
  tft.init();
  tft.setRotation(3); // Landscape

  // Logo Startup
  startupDisplay();
  startTime = millis();
}

void loop() {
  radarSweep();
  delay(50);
}

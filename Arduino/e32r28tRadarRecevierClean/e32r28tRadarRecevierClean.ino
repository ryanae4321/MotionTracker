// Basic Hello World Example 
// Display E32R28T 2.8" Passice Touch Display 
// Display Driver ILI9341
// This program uses the User_Setup.h include file to define all hardware pin mappings 
// and display configuration parameters required by the TFT_eSPI library.
// Special Note: Rememeber to switch the backlight on !

#include <TFT_eSPI.h>

#include <esp_now.h>
#include <WiFi.h>

#include "User_Setup.h"
// #include "screen.h" // this screen includes the text at the bottom of the display
#include "radar_clean.h" // this is the clean version of the screen

// #include <Fonts/GFXFF/FreeSans12pt7b.h>

#define SENSOR_TIMEOUT 2000
#define NUM_SIM_TARGETS 5
#define MAX_DISTANCE 427
#define TFT_BL 21   // Backlight control pin (E32R28T)
#define SD_CS 5

int x = 300;
int y = 10;
int r1 = 7;
int r2 = 5;
int centerX = 160;
int centerY = 175;

float duration, myData;
float realDistance = -1; // Set default for startup
float simDistance[NUM_SIM_TARGETS];
float simAngle[NUM_SIM_TARGETS];
float simSpeed[NUM_SIM_TARGETS];

unsigned long lastPacketTime = 0;

TFT_eSPI tft = TFT_eSPI();

void displayDistance(float distance, float angle) {
  float radius = (distance / MAX_DISTANCE) * centerY;
  int x = centerX - radius * sin(angle);
  int y = centerY - radius * cos(angle);
  if (y < 0) y = 0;

  tft.fillCircle(x, y, 5, TFT_WHITE);
}

void displayDistanceText(float distance) {
  int xStart = 125;
  int yStart = 187;

  tft.setTextColor(TFT_RED);
  tft.setTextDatum(TL_DATUM);

  tft.setFreeFont(&FreeSans12pt7b);
  tft.setTextSize(1);
  int length = tft.drawNumber((int)distance, xStart, yStart);
  xStart += 38;
  tft.setTextFont(1);
  tft.setTextSize(1);
  length = tft.drawString(" - ", xStart, yStart+8);
  length = length + (tft.drawNumber((int)(distance* 10) % 10, xStart+length, yStart+6, 1) / 2);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("cm", xStart+length, yStart+18); 
}

void displayDataText(int contacts, float averageDistance) {
  char data[32];
  int x = 220;
  int y = 195;
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

void simulateTarget(int i) {
  simDistance[i] -= simSpeed[i];

  if (simDistance[i] < 20) {
    simDistance[i] = random(500, 4000) / 10.0;
    simAngle[i] = random(-90, 90) * DEG_TO_RAD;
    simSpeed[i] = random(1, 5);
  }
}

bool sensorAvailable() {
  return millis() - lastPacketTime < SENSOR_TIMEOUT;
}

void OnDataRecv(const esp_now_recv_info* info, const unsigned char* incomingData, int len) {
   memcpy(&realDistance, incomingData, sizeof(realDistance));
  //  Serial.print("Data Received: ");
  //  Serial.println(realDistance);
   lastPacketTime = millis();
}

void headerImage() {
  tft.setSwapBytes(true);
  // tft.pushImage(0, 0, screen_width, screen_height, screen);
  tft.pushImage(0, 0, radar_clean_width, radar_clean_height, radar_clean);
}

void radarSweep() {
  int r = 15;
  while (r < 238) {
    headerImage();
    tft.drawArc(160, 175, r + 5, r, 90, 270, TFT_WHITE, TFT_BLACK, true);
    if (sensorAvailable()) {
      // REAL SENSOR
      displayDistance(realDistance, 0);
      displayDistanceText(realDistance);
    }
    // SIMULATION, always run
    float smallest = MAX_DISTANCE;
    float average = 0;
    for (int i = 0; i < NUM_SIM_TARGETS; i++) {
      simulateTarget(i);
      displayDistance(simDistance[i], simAngle[i]);
      if (simDistance[i] < smallest) smallest = simDistance[i];
      average = (simDistance[i] + average) / 2;
    }
    displayDataText(NUM_SIM_TARGETS, average);
    displayDistanceText(smallest);
    r += 30;
    delay(25);
  }
}

void setup() {
  Serial.begin(115200);
  // Simulation setup
  for (int i = 0; i < NUM_SIM_TARGETS; i++) {
    simDistance[i] = random(500, 4000) / 10.0;
    simAngle[i] = random(-90, 90) * DEG_TO_RAD;
    simSpeed[i] = random(1, 5);
  }
  // Backlight setup
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH); // Turn backlight ON

  // Display init
  tft.init();
  tft.setRotation(1); // Landscape

  // Esp_Now init
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
      Serial.println("Error initializing ESP-NOW");
      return;
  }
  Serial.println(WiFi.macAddress());
  esp_now_register_recv_cb(OnDataRecv);
}

void loop() {
  radarSweep();
  delay(100);
}
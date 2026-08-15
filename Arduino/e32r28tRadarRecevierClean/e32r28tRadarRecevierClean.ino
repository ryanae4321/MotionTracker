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
#include "screen.h" // this screen includes the text at the bottom of the display
#include "radar_clean.h" // this is the clean version of the screen

// #include <Fonts/GFXFF/FreeSans12pt7b.h>

#define MAX_DISTANCE 427
#define TFT_BL 21   // Backlight control pin (E32R28T)
#define SD_CS 5

int x = 300;
int y = 10;
int r1 = 7;
int r2 = 5;

float duration, myData;

float distance = -1; // Set default for startup

TFT_eSPI tft = TFT_eSPI();

void (displayDistance(float distance)) {
  char distDecimals[8];
  int xStart = 125;
  int yStart = 187;
  int x = 160;
  int centerY = 175;
  int y = centerY - (distance / MAX_DISTANCE) * centerY;

  if (y < 0) y = 0;
  if (y > 175) y = 175;

  tft.fillCircle(x, y, 5, TFT_WHITE);
  tft.setTextColor(TFT_RED);
  tft.setTextDatum(TL_DATUM);
  // tft.setTextFont(1);
  // tft.setTextSize(2);
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

void OnDataRecv(const esp_now_recv_info* info, const unsigned char* incomingData, int len) {
   memcpy(&myData, incomingData, sizeof(myData));
  //  Serial.print("Data Received: ");
  //  Serial.println(myData);
   distance = myData;
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
    if (distance > 0) {
      displayDistance(distance);
      // Serial.print("Distance: ");
      // Serial.println(distance);
    }
    r = r + 30;
    delay(25);
  }
}

void setup() {
  Serial.begin(115200);
  // Backlight setup
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);   // Turn backlight ON

  // Display init
  tft.init();
  tft.setRotation(1);           // Landscape

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
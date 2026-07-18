/*
 * HC-SR04 example sketch
 *
 * https://create.arduino.cc/projecthub/Isaac100/getting-started-with-the-hc-sr04-ultrasonic-sensor-036380
 *
 * by Isaac100
 */
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

const int trigPin = 8;
const int echoPin = 9;
const float MAX_DISTANCE = 427.0; // cm (about 14 ft)

float duration, distance;

void setup() {
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  Serial.begin(9600);

  if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED failed");
    while (true);
  }

  oled.clearDisplay();
  oled.display();
}

void loop() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  duration = pulseIn(echoPin, HIGH);
  distance = (duration*.0343)/2;
  displayDistance(distance);
  Serial.print("Distance: ");
  Serial.println(distance);
  delay(100);
}

void displayDistance(float distance)
{
    int x = 64;
    int y = 63 - (distance / MAX_DISTANCE) * 63;

    if (y < 0) y = 0;
    if (y > 63) y = 63;

    oled.clearDisplay();

    oled.drawLine(64, 63, 64, y, SSD1306_WHITE);
    oled.fillCircle(64, y, 3, SSD1306_WHITE);

    oled.setCursor(0, 0);
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    oled.print(distance, 1);
    oled.print(" cm");

    oled.display();
}
// Basic Radar Display Example
// Waveshare ESP32-S3-Touch-LCD-2.8B
// 2.8" 480x640 ST7701 RGB panel

#include <Arduino_GFX_Library.h>
#include "Arduino.h"
#include <Wire.h>
#include <esp_err.h>
#include <esp32-hal-psram.h>
#include <driver/spi_master.h>
#include <initializer_list>
#include "DFRobotDFPlayerMini.h"
#include "radar_clean_full.h"
#include "logo.h"

// The 2.8B uses GPIO 7/15 for I2C, 16 for touch IRQ, and GPIO 35-37 for OPI PSRAM.
#define DFPLAYER_RX_ESP_TX 4
#define DFPLAYER_TX_ESP_RX 44
#define LCD_BACKLIGHT_PIN 6
#define LCD_WIDTH 480
#define LCD_HEIGHT 640
#define UI_SCALE 2
#define UI_WIDTH LCD_HEIGHT
#define LCD_SPI_MOSI 1
#define LCD_SPI_SCLK 2
#define LCD_I2C_SDA 15
#define LCD_I2C_SCL 7
#define LCD_I2C_ADDRESS 0x20
#define LCD_EXPANDER_OUTPUT_REG 0x01
#define LCD_EXPANDER_CONFIG_REG 0x03
#define LCD_EXPANDER_LCD_RESET 1
#define LCD_EXPANDER_LCD_CS 3
#define LCD_EXPANDER_PANEL_ENABLE 8
bool displaySimulation = false;
bool dfPlayerReady = false;
Arduino_ESP32RGBPanel *rgbPanel = new Arduino_ESP32RGBPanel(
    40, 39, 38, 41,
    5, 45, 48, 47, 21,
    14, 13, 12, 11, 10, 9,
    46, 3, 8, 18, 17,
    0, 60, 10, 70,
    0, 20, 10, 20,
    0, 30000000, false, 0, 0, LCD_WIDTH * 10);
Arduino_GFX *gfx = new Arduino_RGB_Display(LCD_WIDTH, LCD_HEIGHT, rgbPanel, 1, true);

// Audio Variables
#if (defined(ARDUINO_AVR_UNO) || defined(ESP8266))
#include <SoftwareSerial.h>
SoftwareSerial softSerial(/*rx =*/4, /*tx =*/5);
#define FPSerial softSerial
#else
#define FPSerial Serial1
#endif
DFRobotDFPlayerMini myDFPlayer;
void printDetail(uint8_t type, int value);

// Sim Variables
#define SENSOR_TIMEOUT 2000
#define NUM_SIM_TARGETS 7
#define MAX_DISTANCE 25
struct TargetInfo {
  float distance, angle, speed;
};
volatile TargetInfo simTargets[NUM_SIM_TARGETS];
int x = 300;
int y = 10;
int r1 = 7;
int r2 = 5;
int centerX = 160 * UI_SCALE;
int centerY = 155 * UI_SCALE;

uint8_t expanderOutput = 0;
spi_device_handle_t lcdCommandDevice = nullptr;

bool writeExpanderRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(LCD_I2C_ADDRESS);
  Wire.write(reg);
  Wire.write(value);
  const uint8_t result = Wire.endTransmission();
  if (result != 0) {
    Serial.printf("LCD I/O expander write failed (register 0x%02X, error %u).\n", reg, result);
    return false;
  }
  return true;
}

bool setExpanderPin(uint8_t pin, bool high) {
  if (pin < 1 || pin > 8) {
    return false;
  }
  const uint8_t mask = 1U << (pin - 1);
  expanderOutput = high ? (expanderOutput | mask) : (expanderOutput & ~mask);
  return writeExpanderRegister(LCD_EXPANDER_OUTPUT_REG, expanderOutput);
}

bool writeSt7701Command(uint8_t command, std::initializer_list<uint8_t> data) {
  spi_transaction_t transaction = {};
  transaction.cmd = 0;
  transaction.addr = command;
  esp_err_t error = spi_device_transmit(lcdCommandDevice, &transaction);
  if (error != ESP_OK) {
    Serial.printf("ST7701 command 0x%02X failed: %s\n", command, esp_err_to_name(error));
    return false;
  }

  for (uint8_t value : data) {
    transaction = {};
    transaction.cmd = 1;
    transaction.addr = value;
    error = spi_device_transmit(lcdCommandDevice, &transaction);
    if (error != ESP_OK) {
      Serial.printf("ST7701 data for command 0x%02X failed: %s\n", command, esp_err_to_name(error));
      return false;
    }
  }
  return true;
}

#define ST7701_WRITE(command, ...) \
  do { if (!writeSt7701Command(command, {__VA_ARGS__})) return false; } while (0)

bool initializeDisplayController() {
  Serial.println("LCD: starting I2C...");
  if (!Wire.begin(LCD_I2C_SDA, LCD_I2C_SCL, 800000)) {
    Serial.println("LCD I2C initialization failed.");
    return false;
  }
  Serial.println("LCD: I2C ready.");
  if (!writeExpanderRegister(LCD_EXPANDER_OUTPUT_REG, 0x00) ||
      !writeExpanderRegister(LCD_EXPANDER_CONFIG_REG, 0x00)) {
    Serial.println("LCD I/O expander initialization failed.");
    return false;
  }
  Serial.println("LCD: I/O expander ready.");
  expanderOutput = 0;
  if (!setExpanderPin(LCD_EXPANDER_PANEL_ENABLE, false) ||
      !setExpanderPin(LCD_EXPANDER_LCD_RESET, false)) {
    Serial.println("LCD I/O expander control failed.");
    return false;
  }
  delay(10);
  if (!setExpanderPin(LCD_EXPANDER_LCD_RESET, true)) {
    Serial.println("LCD reset failed.");
    return false;
  }
  delay(50);
  Serial.println("LCD: panel reset complete.");

  spi_bus_config_t busConfig = {};
  busConfig.mosi_io_num = LCD_SPI_MOSI;
  busConfig.miso_io_num = -1;
  busConfig.sclk_io_num = LCD_SPI_SCLK;
  busConfig.quadwp_io_num = -1;
  busConfig.quadhd_io_num = -1;
  busConfig.max_transfer_sz = 64;
  esp_err_t error = spi_bus_initialize(SPI2_HOST, &busConfig, SPI_DMA_CH_AUTO);
  if (error != ESP_OK) {
    Serial.printf("LCD SPI bus initialization failed: %s\n", esp_err_to_name(error));
    return false;
  }
  Serial.println("LCD: SPI bus ready.");

  spi_device_interface_config_t deviceConfig = {};
  deviceConfig.command_bits = 1;
  deviceConfig.address_bits = 8;
  deviceConfig.mode = SPI_MODE0;
  deviceConfig.clock_speed_hz = 80000000;
  deviceConfig.spics_io_num = -1;
  deviceConfig.queue_size = 1;
  error = spi_bus_add_device(SPI2_HOST, &deviceConfig, &lcdCommandDevice);
  if (error != ESP_OK) {
    Serial.printf("LCD SPI device initialization failed: %s\n", esp_err_to_name(error));
    return false;
  }
  Serial.println("LCD: sending ST7701 initialization...");
  if (!setExpanderPin(LCD_EXPANDER_LCD_CS, false)) {
    Serial.println("LCD chip-select assertion failed.");
    return false;
  }

  ST7701_WRITE(0xFF, 0x77, 0x01, 0x00, 0x00, 0x13);
  ST7701_WRITE(0xEF, 0x08);
  ST7701_WRITE(0xFF, 0x77, 0x01, 0x00, 0x00, 0x10);
  ST7701_WRITE(0xC0, 0x4F, 0x00);
  ST7701_WRITE(0xC1, 0x10, 0x02);
  ST7701_WRITE(0xC2, 0x07, 0x02);
  ST7701_WRITE(0xCC, 0x10);
  ST7701_WRITE(0xB0, 0x00, 0x10, 0x17, 0x0D, 0x11, 0x06, 0x05, 0x08, 0x07, 0x1F, 0x04, 0x11, 0x0E, 0x29, 0x30, 0x1F);
  ST7701_WRITE(0xB1, 0x00, 0x0D, 0x14, 0x0E, 0x11, 0x06, 0x04, 0x08, 0x08, 0x20, 0x05, 0x13, 0x13, 0x26, 0x30, 0x1F);
  ST7701_WRITE(0xFF, 0x77, 0x01, 0x00, 0x00, 0x11);
  ST7701_WRITE(0xB0, 0x65);
  ST7701_WRITE(0xB1, 0x71);
  ST7701_WRITE(0xB2, 0x82);
  ST7701_WRITE(0xB3, 0x80);
  ST7701_WRITE(0xB5, 0x42);
  ST7701_WRITE(0xB7, 0x85);
  ST7701_WRITE(0xB8, 0x20);
  ST7701_WRITE(0xC0, 0x09);
  ST7701_WRITE(0xC1, 0x78);
  ST7701_WRITE(0xC2, 0x78);
  ST7701_WRITE(0xD0, 0x88);
  ST7701_WRITE(0xEE, 0x42);
  ST7701_WRITE(0xE0, 0x00, 0x00, 0x02);
  ST7701_WRITE(0xE1, 0x04, 0xA0, 0x06, 0xA0, 0x05, 0xA0, 0x07, 0xA0, 0x00, 0x44, 0x44);
  ST7701_WRITE(0xE2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
  ST7701_WRITE(0xE3, 0x00, 0x00, 0x22, 0x22);
  ST7701_WRITE(0xE4, 0x44, 0x44);
  ST7701_WRITE(0xE5, 0x0C, 0x90, 0xA0, 0xA0, 0x0E, 0x92, 0xA0, 0xA0, 0x08, 0x8C, 0xA0, 0xA0, 0x0A, 0x8E, 0xA0, 0xA0);
  ST7701_WRITE(0xE6, 0x00, 0x00, 0x22, 0x22);
  ST7701_WRITE(0xE7, 0x44, 0x44);
  ST7701_WRITE(0xE8, 0x0D, 0x91, 0xA0, 0xA0, 0x0F, 0x93, 0xA0, 0xA0, 0x09, 0x8D, 0xA0, 0xA0, 0x0B, 0x8F, 0xA0, 0xA0);
  ST7701_WRITE(0xEB, 0x00, 0x00, 0xE4, 0xE4, 0x44, 0x00, 0x40);
  ST7701_WRITE(0xED, 0xFF, 0xF5, 0x47, 0x6F, 0x0B, 0xA1, 0xAB, 0xFF, 0xFF, 0xBA, 0x1A, 0xB0, 0xF6, 0x74, 0x5F, 0xFF);
  ST7701_WRITE(0xEF, 0x08, 0x08, 0x08, 0x40, 0x3F, 0x64);
  ST7701_WRITE(0xFF, 0x77, 0x01, 0x00, 0x00, 0x00);
  ST7701_WRITE(0xFF, 0x77, 0x01, 0x00, 0x00, 0x13);
  ST7701_WRITE(0xE6, 0x16, 0x7C);
  ST7701_WRITE(0xE8, 0x00, 0x0E);
  ST7701_WRITE(0xFF, 0x77, 0x01, 0x00, 0x00, 0x00);
  ST7701_WRITE(0x11, 0x00);
  delay(200);
  ST7701_WRITE(0xFF, 0x77, 0x01, 0x00, 0x00, 0x13);
  ST7701_WRITE(0xE8, 0x00, 0x0C);
  delay(150);
  ST7701_WRITE(0xE8, 0x00, 0x00);
  ST7701_WRITE(0xFF, 0x77, 0x01, 0x00, 0x00, 0x00);
  ST7701_WRITE(0x29, 0x00);
  ST7701_WRITE(0x35, 0x00);
  ST7701_WRITE(0x11, 0x00);
  delay(200);
  ST7701_WRITE(0x29, 0x00);
  ST7701_WRITE(0x29, 0x00);
  delay(100);

  if (!setExpanderPin(LCD_EXPANDER_LCD_CS, true)) {
    Serial.println("LCD chip-select release failed.");
    return false;
  }
  Serial.println("LCD: ST7701 initialization complete.");
  return true;
}

#undef ST7701_WRITE

void drawBitmap2x(int16_t x, int16_t y, const uint16_t *bitmap, int16_t width, int16_t height) {
  uint16_t scaledRow[UI_WIDTH];
  for (int16_t row = 0; row < height; ++row) {
    for (int16_t column = 0; column < width; ++column) {
      const uint16_t color = pgm_read_word(bitmap + row * width + column);
      scaledRow[column * UI_SCALE] = color;
      scaledRow[column * UI_SCALE + 1] = color;
    }
    gfx->draw16bitRGBBitmap(x, y + row * UI_SCALE, scaledRow, width * UI_SCALE, 1);
    gfx->draw16bitRGBBitmap(x, y + row * UI_SCALE + 1, scaledRow, width * UI_SCALE, 1);
  }
}

// ------------------------------------------------
// Simulation Task
// ------------------------------------------------
void SimTask(void *pvParameters) {
  for (;;) {
    if (displaySimulation) {
      for (int i = 0; i < NUM_SIM_TARGETS; i++) {
        simTargets[i].distance -= simTargets[i].speed * 0.05;
        if (simTargets[i].distance < 0.5) {
          simTargets[i].distance = random(150, 250) / 10.0;
          simTargets[i].angle = random(-90, 90) * DEG_TO_RAD;
          simTargets[i].speed = random(1, 5);
        }
      }
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

// ----------------------------------------------
// Display Task and Functions
// ----------------------------------------------
void headerImage() {
  drawBitmap2x(0, 0, radar_clean_full, radar_clean_full_width, radar_clean_full_height);
}

void logoImage() {
  drawBitmap2x(0, 0, Weyland_Yutani_Logo, Weyland_Yutani_Logo_width, Weyland_Yutani_Logo_height);
}

void startupDisplay() {
  const int barX = 45 * UI_SCALE;
  const int barY = 195 * UI_SCALE;
  const int barWidth = 230 * UI_SCALE;
  const int barHeight = 16 * UI_SCALE;
  const unsigned long duration = 5000;
  unsigned long startTime = millis();
  while (millis() - startTime < duration) {
    logoImage();
    gfx->drawRect(barX, barY, barWidth, barHeight, RGB565_WHITE);
    unsigned long elapsed = millis() - startTime;
    int progress = (elapsed * 100) / duration;
    int fillWidth = ((barWidth - 4) * progress) / 100;
    gfx->fillRect(barX + 2, barY + 2, barWidth - 4, barHeight - 4, RGB565_BLACK);
    if (fillWidth > 0) {
      gfx->fillRect(barX + 2, barY + 2, fillWidth, barHeight - 4, RGB565_WHITE);
    }
  }
  gfx->fillRect(barX + 2, barY + 2, barWidth - 4, barHeight - 4, RGB565_WHITE);
}

void displayDistance(float distance, float angle) {
  float radius = (distance / MAX_DISTANCE) * centerY;
  int targetX = centerX - radius * sin(angle);
  int targetY = centerY - radius * cos(angle);
  if (targetY < 0) targetY = 0;
  gfx->fillCircle(targetX, targetY, 8 * UI_SCALE, RGB565_WHITE);
}

void displayDistanceText(float distance) {
  char integerPart[16];
  char decimalPart[16];
  int xStart = 128;
  int yStart = 165;
  int integerWidth;
  sprintf(integerPart, "%02d", (int)distance);
  sprintf(decimalPart, "%.2f", distance - (int)distance);
  gfx->setTextColor(RGB565_RED);
  gfx->setTextSize(2 * UI_SCALE);
  gfx->setCursor(xStart * UI_SCALE, yStart * UI_SCALE);
  gfx->print(integerPart);
  integerWidth = strlen(integerPart) * 12 * UI_SCALE;
  gfx->setTextSize(UI_SCALE);
  gfx->setCursor(xStart * UI_SCALE + integerWidth + 2 * UI_SCALE, yStart * UI_SCALE + 6 * UI_SCALE);
  gfx->print(" - ");
  gfx->setCursor(xStart * UI_SCALE + integerWidth + 25 * UI_SCALE, yStart * UI_SCALE + 2 * UI_SCALE);
  gfx->print(decimalPart + 2);
  gfx->setCursor(xStart * UI_SCALE + integerWidth + 30 * UI_SCALE, yStart * UI_SCALE + 14 * UI_SCALE);
  gfx->print("m");
}

void displayRightText(float averageDistance) {
  char data[32];
  int x = 220 * UI_SCALE;
  int y = 173 * UI_SCALE;
  sprintf(data, "C.X. %d/%.2f", NUM_SIM_TARGETS, averageDistance);
  gfx->setTextColor(RGB565_WHITE);
  gfx->setTextSize(UI_SCALE);
  gfx->setCursor(x, y);
  gfx->print(data);
}

void displayLeftText() {
  gfx->setTextColor(RGB565_WHITE);
  gfx->setTextSize(UI_SCALE);
  gfx->setCursor(25 * UI_SCALE, 173 * UI_SCALE);
  gfx->print("F.E.M.S. 9.47.2");
}

void DisplayTask(void *pvParameters) {
  for (;;) {
    if (dfPlayerReady && !displaySimulation) myDFPlayer.playMp3Folder(2);
    int radarRadius = 15;
    while (radarRadius < 270) {
      headerImage();
      displayLeftText();
      gfx->fillArc(160 * UI_SCALE, 164 * UI_SCALE, (radarRadius + 5) * UI_SCALE, radarRadius * UI_SCALE, 180, 360, RGB565_WHITE);
      radarRadius += 30;
      float smallest = MAX_DISTANCE;
      float average = 0;
      if (displaySimulation) {
        for (int i = 0; i < NUM_SIM_TARGETS; i++) {
          displayDistance(simTargets[i].distance, simTargets[i].angle);
          if (simTargets[i].distance < smallest) smallest = simTargets[i].distance;
          average += simTargets[i].distance;
        }
      } else {
        smallest = 0.0;
      }
      displayDistanceText(smallest);
      displayRightText(average/NUM_SIM_TARGETS);
    }
    radarRadius = 15;
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

// ---------------------------------------------
// Audio Task
// ---------------------------------------------
void AudioTask(void *pvParameters) {
  for (;;) {
    if (dfPlayerReady && displaySimulation) {
      myDFPlayer.playMp3Folder(1);
    }
    vTaskDelay(750 / portTICK_PERIOD_MS);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("ESP32 online.");
  if (!psramFound()) {
    Serial.println("PSRAM not detected. Select Tools > PSRAM > OPI PSRAM and upload again.");
    while (true) {
      delay(1000);
    }
  }
  Serial.printf("PSRAM ready: %u bytes free.\n", ESP.getFreePsram());
#if (defined ESP32)
  FPSerial.begin(9600, SERIAL_8N1, /*rx =*/DFPLAYER_TX_ESP_RX, /*tx =*/DFPLAYER_RX_ESP_TX);
#else
  FPSerial.begin(9600);
#endif

  // Audio setup
  Serial.println();
  Serial.println(F("Initializing DFPlayer ... (May take 3~5 seconds)"));

  if (!myDFPlayer.begin(FPSerial, /*isACK = */true, /*doReset = */true)) {  //Use serial to communicate with mp3.
    Serial.println(F("DFPlayer begin() failed!"));
  } else {
    dfPlayerReady = true;
    Serial.println(F("DFPlayer Mini online."));
    myDFPlayer.volume(17);
  }

  // Lcd setup
  Serial.println("LCD: starting display controller...");
  if (!initializeDisplayController()) {
    Serial.println("LCD controller initialization failed.");
    while (true) {
      delay(1000);
    }
  }
  Serial.println("LCD: starting RGB framebuffer...");
  if (!gfx->begin()) {
    Serial.println("LCD begin() failed!");
    while (true) {
      delay(1000);
    }
  }
  Serial.println("LCD: RGB framebuffer ready.");
  pinMode(LCD_BACKLIGHT_PIN, OUTPUT);
  digitalWrite(LCD_BACKLIGHT_PIN, HIGH);
  gfx->fillScreen(RGB565_BLACK);
  Serial.println();
  Serial.println(F("LCD online."));
  startupDisplay();
  Serial.println("LCD: startup screen complete.");

  // Sim setup
  for (int i = 0; i < NUM_SIM_TARGETS; i++) {
    simTargets[i].distance = random(150, 250) / 10.0;
    simTargets[i].angle = random(-90, 90) * DEG_TO_RAD;
    simTargets[i].speed = random(1, 5);
  }

  // Task setup
  xTaskCreatePinnedToCore(SimTask, "SimTask", 4096, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(DisplayTask, "DisplayTask", 8192, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(AudioTask, "AudioTask", 4096, NULL, 1, NULL, 1);
}

void loop() {
  static unsigned long timer = millis();
  if (millis() - timer > 10000) {
    timer = millis();
    displaySimulation = true;
  }
  vTaskDelay(250 / portTICK_PERIOD_MS);
}

void printDetail(uint8_t type, int value) {
  switch (type) {
    case TimeOut:
      Serial.println(F("Time Out!"));
      break;
    case WrongStack:
      Serial.println(F("Stack Wrong!"));
      break;
    case DFPlayerCardInserted:
      Serial.println(F("Card Inserted!"));
      break;
    case DFPlayerCardRemoved:
      Serial.println(F("Card Removed!"));
      break;
    case DFPlayerCardOnline:
      Serial.println(F("Card Online!"));
      break;
    case DFPlayerUSBInserted:
      Serial.println("USB Inserted!");
      break;
    case DFPlayerUSBRemoved:
      Serial.println("USB Removed!");
      break;
    case DFPlayerPlayFinished:
      Serial.print(F("Number:"));
      Serial.print(value);
      Serial.println(F(" Play Finished!"));
      break;
    case DFPlayerError:
      Serial.print(F("DFPlayerError:"));
      switch (value) {
        case Busy:
          Serial.println(F("Card not found"));
          break;
        case Sleeping:
          Serial.println(F("Sleeping"));
          break;
        case SerialWrongStack:
          Serial.println(F("Get Wrong Stack"));
          break;
        case CheckSumNotMatch:
          Serial.println(F("Check Sum Not Match"));
          break;
        case FileIndexOut:
          Serial.println(F("File Index Out of Bound"));
          break;
        case FileMismatch:
          Serial.println(F("Cannot Find File"));
          break;
        case Advertise:
          Serial.println(F("In Advertise"));
          break;
        default:
          break;
      }
      break;
    default:
      break;
  }
}

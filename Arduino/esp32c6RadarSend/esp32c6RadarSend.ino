#include <esp_now.h>
#include <WiFi.h>
// Receiver MAC Address
uint8_t broadcastAddress[] = {0xB4, 0xBF, 0xE9, 0x10, 0xD4, 0xC8};

#define TRIG_PIN 1
#define ECHO_PIN 0

float duration, myData;

void OnDataSent(const wifi_tx_info_t* info, esp_now_send_status_t status) {
   Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

void setup() {
  Serial.begin(115200);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // Esp_Now init
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
      Serial.println("Error initializing ESP-NOW");
      return;
  }
  Serial.println(WiFi.macAddress());
   esp_now_register_send_cb(OnDataSent);
   esp_now_peer_info_t peerInfo;
   memcpy(peerInfo.peer_addr, broadcastAddress, 6);
   peerInfo.channel = 0;
   peerInfo.encrypt = false;
   esp_now_add_peer(&peerInfo);
}

void loop() {
  digitalWrite(TRIG_PIN, LOW);
  delay(2);
  digitalWrite(TRIG_PIN, HIGH);
  delay(10);
  digitalWrite(TRIG_PIN, LOW);
  duration = pulseIn(ECHO_PIN, HIGH);
  myData = (duration*.0343)/2;
  Serial.print("Distance: ");
  Serial.println(myData);
  esp_now_send(broadcastAddress, (uint8_t *)&myData, sizeof(myData));
  delay(500);
}
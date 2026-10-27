#include <Arduino.h>
#include <LD2450.h>
#include <esp_now.h>
#include <WiFi.h>

LD2450 ld2450;

// Display ESP MAC address
uint8_t displayAddress[] = {
    0xB8, 0x1F, 0x3F, 0xCA, 0x32, 0x6C
};

constexpr uint8_t MAX_TARGETS = 3;

struct RadarPacket
{
    uint32_t sequence;
    uint8_t targetCount;
    int16_t x[MAX_TARGETS];
    int16_t y[MAX_TARGETS];
};

RadarPacket radarPacket = {};
uint32_t sequenceNumber = 0;

void OnDataSent(
    const wifi_tx_info_t *info,
    esp_now_send_status_t status)
{
    if (status != ESP_NOW_SEND_SUCCESS)
    {
        Serial.println("ESP-NOW delivery failed");
    }
}

void setup()
{
    Serial.begin(115200);
    delay(1000);

    /*
        LD2450 TX -> XIAO D1
        LD2450 RX -> XIAO D0
    */
    Serial1.begin(
        256000,
        SERIAL_8N1,
        D1,  // ESP RX
        D0   // ESP TX
    );

    ld2450.begin(Serial1, false);
    Serial.println("LD2450 started");

    // Initialize ESP-NOW
    WiFi.mode(WIFI_STA);

    Serial.print("Sender MAC address: ");
    Serial.println(WiFi.macAddress());

    if (esp_now_init() != ESP_OK)
    {
        Serial.println("Error initializing ESP-NOW");
        return;
    }

    esp_now_register_send_cb(OnDataSent);

    // Always initialize this structure before using it
    esp_now_peer_info_t peerInfo = {};

    memcpy(
        peerInfo.peer_addr,
        displayAddress,
        sizeof(displayAddress)
    );

    peerInfo.channel = 0;
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) != ESP_OK)
    {
        Serial.println("Failed to add ESP-NOW peer");
        return;
    }

    Serial.println("ESP-NOW peer added");
}

void loop()
{
    // Only construct and send a packet when a new radar frame arrives
    if (ld2450.read() <= 0)
    {
        return;
    }

    // Clear old coordinates from the previous packet
    memset(radarPacket.x, 0, sizeof(radarPacket.x));
    memset(radarPacket.y, 0, sizeof(radarPacket.y));

    radarPacket.sequence = sequenceNumber++;
    radarPacket.targetCount = 0;

    int supportedTargets =
        ld2450.getSensorSupportedTargetCount();

    // Prevent an unexpected library value from exceeding our arrays
    if (supportedTargets > MAX_TARGETS)
    {
        supportedTargets = MAX_TARGETS;
    }

    for (int i = 0; i < supportedTargets; i++)
    {
        const LD2450::RadarTarget target =
            ld2450.getTarget(i);

        if (!target.valid)
        {
            continue;
        }

        uint8_t slot = radarPacket.targetCount;

        radarPacket.x[slot] = target.x;
        radarPacket.y[slot] = target.y;

        radarPacket.targetCount++;

        Serial.print("Target ");
        Serial.print(slot + 1);
        Serial.print(": X=");
        Serial.print(target.x);
        Serial.print(" mm, Y=");
        Serial.print(target.y);
        Serial.println(" mm");
    }

    Serial.print("Sending ");
    Serial.print(radarPacket.targetCount);
    Serial.println(" target(s)");

    esp_err_t result = esp_now_send(
        displayAddress,
        reinterpret_cast<const uint8_t *>(&radarPacket),
        sizeof(radarPacket)
    );

    if (result != ESP_OK)
    {
        Serial.print("esp_now_send error: ");
        Serial.println(result);
    }
}
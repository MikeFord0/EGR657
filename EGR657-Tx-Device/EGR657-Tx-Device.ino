#include "ESP32_NOW.h"
#include "WiFi.h"

#include <esp_mac.h>  // For the MAC2STR and MACSTR macros

#include <SPI.h>
#include <SD.h>

/* Definitions */

#define ESPNOW_WIFI_CHANNEL 6

const int chipSelect = 5;  // Adjust based on your SD module wiring
const char* filename = "/data_log.csv";

/* Classes */

// Creating a new class that inherits from the ESP_NOW_Peer class is required.

class ESP_NOW_Broadcast_Peer : public ESP_NOW_Peer {
public:
  // Constructor of the class using the broadcast address
  ESP_NOW_Broadcast_Peer(uint8_t channel, wifi_interface_t iface, const uint8_t *lmk) : ESP_NOW_Peer(ESP_NOW.BROADCAST_ADDR, channel, iface, lmk) {}

  // Destructor of the class
  ~ESP_NOW_Broadcast_Peer() {
    remove();
  }

  // Function to properly initialize the ESP-NOW and register the broadcast peer
  bool begin() {
    if (!ESP_NOW.begin() || !add()) {
      log_e("Failed to initialize ESP-NOW or register the broadcast peer");
      return false;
    }
    return true;
  }

  // Function to send a message to all devices within the network
  bool send_message(const uint8_t *data, size_t len) {
    if (!send(data, len)) {
      log_e("Failed to broadcast message");
      return false;
    }
    return true;
  }
};

/* Global Variables */
#define SLEEP_TIME (3600e6) / 60 / 30  // 2 seconds in microseconds

uint32_t msg_count = 0;

// Create a broadcast peer object
ESP_NOW_Broadcast_Peer broadcast_peer(ESPNOW_WIFI_CHANNEL, WIFI_IF_STA, NULL);

/* Main */

struct DataPacket {
    float temperature;
    float lightLevel;
    float humidity;
    float soilMoisture;
    float solarCurrent;
    float solarVoltage;
    float loadCurrent;
    float loadVoltage;
};

   DataPacket dataPacket;

// ---------------------- SD INIT FUNCTION ----------------------
void initializeSDCard() {

    if (!SD.begin(chipSelect)) {
        Serial.println("SD card initialization failed!");
    }
    else
    {
      Serial.println("SD card initialized.");
    }
    // Create CSV header if file doesn't exist
    if (!SD.exists(filename)) {
        File file = SD.open(filename, FILE_WRITE);
        if (file) {
            file.println("Temperature,LightLevel,Humidity,SoilMoisture,SolarCurrent,SolarVoltage,LoadCurrent,LoadVoltage");
            file.close();
            Serial.println("CSV header written.");
        } else {
            Serial.println("Failed to create CSV file!");
        }
    }
}

// ---------------------- LOGGING FUNCTION ----------------------
void logDataToSD(const DataPacket& packet) {
    File file = SD.open(filename, FILE_APPEND);
    if (file) {

        file.print(packet.temperature);   file.print(",");
        file.print(packet.lightLevel);    file.print(",");
        file.print(packet.humidity);      file.print(",");
        file.print(packet.soilMoisture);  file.print(",");
        file.print(packet.solarCurrent);  file.print(",");
        file.print(packet.solarVoltage);  file.print(",");
        file.print(packet.loadCurrent);   file.print(",");
        file.println(packet.loadVoltage);
        file.close();
        Serial.println("Data logged to SD card.");
    } else {
        Serial.println("Failed to open CSV file for appending.");
    }
}

void setup() {
  Serial.begin(115200);

  // Initialize the Wi-Fi module
  WiFi.mode(WIFI_STA);
  WiFi.setChannel(ESPNOW_WIFI_CHANNEL);
  while (!WiFi.STA.started()) {
    delay(100);
  }
            delay(500);  // Ensure serial output finishes before sleep


  // Register the broadcast peer
  if (!broadcast_peer.begin()) {
    Serial.println("Failed to initialize broadcast peer");
    Serial.println("Reebooting in 5 seconds...");
    delay(5000);
    ESP.restart();
  }
          delay(500);  // Ensure serial output finishes before sleep

initializeSDCard();
//-----------------------------------------------------------------------
//TODO: Insert sensor setup and getting actua data, possibly modify delays 
//------------------------------------------------------------------------


    // Populate data packet with random values
    dataPacket.temperature = random(200, 350) * 0.1;  // 20.0 to 35.0 °C
    dataPacket.lightLevel = random(100, 1000);  // Arbitrary light level
    dataPacket.humidity = random(30, 80);  // 30% - 80% humidity
    dataPacket.soilMoisture = random(200, 800);  
    dataPacket.solarCurrent = random(0, 500) * 0.01;  // 0.00 - 5.00 A
    dataPacket.solarVoltage = random(1000, 2000) * 0.01;  // 10.00 - 20.00 V
    dataPacket.loadCurrent = random(0, 500) * 0.01;  
    dataPacket.loadVoltage = random(500, 1200) * 0.01;

logDataToSD(dataPacket);

      if (!broadcast_peer.send_message((uint8_t *)&dataPacket, sizeof(dataPacket))) {
    Serial.println("Failed to broadcast message");
  }
  else{
    Serial.println("Sent broadcast message");
  }
          delay(1000);  // Ensure serial output finishes before sleep

            Serial.println("Going to sleep...");
    esp_deep_sleep(SLEEP_TIME);
}

void loop() {

}
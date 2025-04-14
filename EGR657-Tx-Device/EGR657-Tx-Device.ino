#include "ESP32_NOW.h"
#include "WiFi.h"

#include <esp_mac.h>  // For the MAC2STR and MACSTR macros

#include <SPI.h>
#include <SD.h>

#include "DHT.h"  // Library for DHT sensors

#include <Wire.h>
#include <Adafruit_INA219.h>

//Pin defines
#define HUMIDITY_AND_TEMP_PIN 4  // data pin
#define RELAY_PIN 1
#define SOIL_MOISTURE_PIN 34
#define PHOTO_PIN 35   // Use a pin that supports ADC (e.g., 32-39)
#define dhtType DHT22  // DHT 22 (AM2302), AM2321
#define PANEL_SENSOR_ADDRESS 0x40
#define LOAD_SENSOR_ADDRESS 0x41
#define chipSelect 5
#define SLEEP_TIME (3600e6) / 60 / 30  // 2 seconds in microseconds

#define ESPNOW_WIFI_CHANNEL 6

//Temp, humidity, and voltage sensor objects
Adafruit_INA219 panelSensor(PANEL_SENSOR_ADDRESS);
Adafruit_INA219 loadSensor(LOAD_SENSOR_ADDRESS);
DHT dht(HUMIDITY_AND_TEMP_PIN, dhtType);

//SD card file name
const char* filename = "/data_log.csv";

// Creating a new class that inherits from the ESP_NOW_Peer class
class ESP_NOW_Broadcast_Peer : public ESP_NOW_Peer {
public:
  // Constructor of the class using the broadcast address
  ESP_NOW_Broadcast_Peer(uint8_t channel, wifi_interface_t iface, const uint8_t* lmk)
    : ESP_NOW_Peer(ESP_NOW.BROADCAST_ADDR, channel, iface, lmk) {}

  // Destructor of the class
  ~ESP_NOW_Broadcast_Peer() {
    remove();
  }

  // Function to initialize the ESP-NOW and register the broadcast peer
  bool begin() {
    if (!ESP_NOW.begin() || !add()) {
      log_e("Failed to initialize ESP-NOW or register the broadcast peer");
      return false;
    }
    return true;
  }

  // Function to send a message to all devices within the network
  bool send_message(const uint8_t* data, size_t len) {
    if (!send(data, len)) {
      log_e("Failed to broadcast message");
      return false;
    }
    return true;
  }
};

// Create a broadcast peer object
ESP_NOW_Broadcast_Peer broadcast_peer(ESPNOW_WIFI_CHANNEL, WIFI_IF_STA, NULL);

//Data struct to hold all environmental data
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
  } else {
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

    file.print(packet.temperature);
    file.print(",");
    file.print(packet.lightLevel);
    file.print(",");
    file.print(packet.humidity);
    file.print(",");
    file.print(packet.soilMoisture);
    file.print(",");
    file.print(packet.solarCurrent);
    file.print(",");
    file.print(packet.solarVoltage);
    file.print(",");
    file.print(packet.loadCurrent);
    file.print(",");
    file.println(packet.loadVoltage);
    file.close();
    Serial.println("Data logged to SD card.");
  } else {
    Serial.println("Failed to open CSV file for appending.");
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n=== Starting System Initialization ===");

  // SD CARD
  Serial.println("Initializing SD card...");
  initializeSDCard();

  // DHT SENSOR
  Serial.println("Initializing DHT sensor...");
  dht.begin();
  delay(1000);  // Give it time to settle

  // INA219 PANEL SENSOR
  Serial.println("Initializing panel INA219 sensor (0x40)...");
  if (!panelSensor.begin()) {
    Serial.println("[ERROR] Failed to find INA219 panel sensor at address 0x40");
    while (1) { delay(10); }
  }
  panelSensor.setCalibration_32V_1A();
  Serial.println("Panel INA219 initialized and calibrated.");

  // INA219 LOAD SENSOR
  Serial.println("Initializing load INA219 sensor (0x41)...");
  if (!loadSensor.begin()) {
    Serial.println("[ERROR] Failed to find INA219 load sensor at address 0x41");
    while (1) { delay(10); }
  }
  loadSensor.setCalibration_32V_1A();
  Serial.println("Load INA219 initialized and calibrated.");

  // PHOTODIODE
  Serial.println("Initializing photodiode (ADC on pin 35)...");
  analogReadResolution(12);
  pinMode(PHOTO_PIN, INPUT);
  delay(500);  // Let it settle

  int lightValue = analogRead(PHOTO_PIN);
  Serial.print("Raw light sensor value: ");
  Serial.println(lightValue);

  if (lightValue > 4000) {
    dataPacket.lightLevel = 1;
    Serial.println("Light level: 1 (Not very sunny)");
  } else if (lightValue > 3000) {
    dataPacket.lightLevel = 2;
    Serial.println("Light level: 2 (Moderately sunny)");
  } else {
    dataPacket.lightLevel = 3;
    Serial.println("Light level: 3 (Very sunny)");
  }

  // SOIL MOISTURE
  Serial.println("Reading soil moisture sensor...");
  dataPacket.soilMoisture = analogRead(SOIL_MOISTURE_PIN);
  Serial.print("Soil moisture raw value: ");
  Serial.println(dataPacket.soilMoisture);

  // DHT DATA
  Serial.println("Reading DHT sensor data...");
  dataPacket.humidity = dht.readHumidity();
  dataPacket.temperature = dht.readTemperature(true);  // Fahrenheit

  if (isnan(dataPacket.humidity) || isnan(dataPacket.temperature)) {
    Serial.println("[WARNING] Failed to read temperature/humidity from DHT!");
  } else {
    Serial.print("Humidity: ");
    Serial.println(dataPacket.humidity);
    Serial.print("Temperature (F): ");
    Serial.println(dataPacket.temperature);
  }

  // PANEL SENSOR DATA
  Serial.println("Reading panel sensor...");
  dataPacket.solarVoltage = panelSensor.getBusVoltage_V();
  dataPacket.solarCurrent = panelSensor.getCurrent_mA();
  Serial.print("Solar Voltage (V): ");
  Serial.println(dataPacket.solarVoltage);
  Serial.print("Solar Current (mA): ");
  Serial.println(dataPacket.solarCurrent);

  // LOAD SENSOR DATA
  Serial.println("Reading load sensor...");
  dataPacket.loadVoltage = loadSensor.getBusVoltage_V();
  dataPacket.loadCurrent = loadSensor.getCurrent_mA();
  Serial.print("Load Voltage (V): ");
  Serial.println(dataPacket.loadVoltage);
  Serial.print("Load Current (mA): ");
  Serial.println(dataPacket.loadCurrent);

  // SD CARD LOGGING
  Serial.println("Logging data to SD card...");
  logDataToSD(dataPacket);

  // WI-FI INIT
  Serial.println("Initializing Wi-Fi for ESP-NOW...");
  WiFi.mode(WIFI_STA);
  WiFi.setChannel(ESPNOW_WIFI_CHANNEL);

  while (!WiFi.STA.started()) {
    Serial.print(".");
    delay(100);
  }
  Serial.println("\nWi-Fi station mode started.");

  // ESP-NOW INIT
  Serial.println("Registering ESP-NOW broadcast peer...");
  if (!broadcast_peer.begin()) {
    Serial.println("[ERROR] Failed to initialize broadcast peer!");
    Serial.println("Rebooting in 5 seconds...");
    delay(5000);
    ESP.restart();
  }

  Serial.println("Sending data via ESP-NOW...");
  if (!broadcast_peer.send_message((uint8_t*)&dataPacket, sizeof(dataPacket))) {
    Serial.println("[ERROR] Failed to broadcast message");
  } else {
    Serial.println("Broadcast message sent successfully.");
  }

  Serial.println("Setup complete. Going to deep sleep in 1 second...");
  delay(1000);
  esp_deep_sleep(SLEEP_TIME);
}


void loop() {
}
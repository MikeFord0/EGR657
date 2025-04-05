#include "ESP32_NOW.h"
#include "WiFi.h"

#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>  // Ensure you install AsyncTCP, not ESPAsyncTCP
#include <ArduinoJson.h>

#include <esp_mac.h>  // For the MAC2STR and MACSTR macros

#include <vector>

/* Definitions */

#define ESPNOW_WIFI_CHANNEL 6
#define MAX_SAMPLES 10

//DATA Packet
struct DataPacket {
    float temperature;       // Temperature sensor reading
    float lightLevel;        // Light level (e.g., from an LDR)
    float humidity;          // Humidity sensor reading
    float soilMoisture;      // Soil moisture level
    float solarCurrent;      // Current of the solar panel
    float solarVoltage;      // Voltage of the solar panel
    float loadCurrent;       // Current of the load
    float loadVoltage;       // Voltage of the load
};

DataPacket receivedData[MAX_SAMPLES];
int receivedCount = 0;

// Web server instance
AsyncWebServer server(80);
/* Classes */

// Creating a new class that inherits from the ESP_NOW_Peer class is required.

class ESP_NOW_Peer_Class : public ESP_NOW_Peer {
public:
  // Constructor of the class
  ESP_NOW_Peer_Class(const uint8_t *mac_addr, uint8_t channel, wifi_interface_t iface, const uint8_t *lmk) : ESP_NOW_Peer(mac_addr, channel, iface, lmk) {}

  // Destructor of the class
  ~ESP_NOW_Peer_Class() {}

  // Function to register the master peer
  bool add_peer() {
    if (!add()) {
      log_e("Failed to register the broadcast peer");
      return false;
    }
    return true;
  }

  // Function to print the received messages from the master
  void onReceive(const uint8_t *data, size_t len, bool broadcast) {
   if (receivedCount < MAX_SAMPLES) {
        memcpy(&receivedData[receivedCount], data, len);
        receivedCount++;
    } else {
        // When MAX_SAMPLES is reached, overwrite the oldest data (circular buffer)
        for (int i = 0; i < MAX_SAMPLES - 1; i++) {
            receivedData[i] = receivedData[i + 1];  // Shift all the data
        }
        // Copy the new data into the last slot
        memcpy(&receivedData[MAX_SAMPLES - 1], data, len);
    }

    Serial.println("Data received!");
  }
};

// Serve Web Page
const char webpage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>ESP32 Sensor Data</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
</head>
<body>
    <h2>ESP32 Sensor Data</h2>
    
    <div style="width: 45%; display: inline-block;">
        <canvas id="temperatureChart"></canvas>
        <canvas id="humidityChart"></canvas>
        <canvas id="lightChart"></canvas>
        <canvas id="soilMoistureChart"></canvas>
    </div>
    
    <div style="width: 45%; display: inline-block;">
        <canvas id="solarCurrentChart"></canvas>
        <canvas id="solarVoltageChart"></canvas>
        <canvas id="loadCurrentChart"></canvas>
        <canvas id="loadVoltageChart"></canvas>
    </div>

    <script>
        async function fetchData() {
            const response = await fetch('/data');
            const json = await response.json();

            const labels = json.map((_, index) => 'Sample ' + (index + 1));
            
            const temperatureData = json.map(item => item.temperature);
            const humidityData = json.map(item => item.humidity);
            const lightData = json.map(item => item.lightLevel);
            const soilMoistureData = json.map(item => item.soilMoisture);
            const solarCurrentData = json.map(item => item.solarCurrent);
            const solarVoltageData = json.map(item => item.solarVoltage);
            const loadCurrentData = json.map(item => item.loadCurrent);
            const loadVoltageData = json.map(item => item.loadVoltage);

            function createChart(chartId, label, data, borderColor) {
                new Chart(document.getElementById(chartId), {
                    type: 'line',
                    data: {
                        labels: labels,
                        datasets: [{ label: label, data: data, borderColor: borderColor, fill: false }]
                    }
                });
            }

            createChart("temperatureChart", "Temperature (°C)", temperatureData, "red");
            createChart("humidityChart", "Humidity (%)", humidityData, "blue");
            createChart("lightChart", "Light Level", lightData, "orange");
            createChart("soilMoistureChart", "Soil Moisture", soilMoistureData, "green");
            createChart("solarCurrentChart", "Solar Panel Current (A)", solarCurrentData, "purple");
            createChart("solarVoltageChart", "Solar Panel Voltage (V)", solarVoltageData, "brown");
            createChart("loadCurrentChart", "Load Current (A)", loadCurrentData, "black");
            createChart("loadVoltageChart", "Load Voltage (V)", loadVoltageData, "pink");
        }

    function updateCharts() {
        fetchData();
        setTimeout(updateCharts, 120000);  // Refresh every 2 minutes
    }

    updateCharts(); // Start auto-refreshing data
    </script>
</body>
</html>
)rawliteral";


String getDataJson() {
    DynamicJsonDocument doc(1024);
    JsonArray arr = doc.to<JsonArray>();

    for (int i = 0; i < receivedCount; i++) {
        JsonObject obj = arr.createNestedObject();
        obj["temperature"] = receivedData[i].temperature;
        obj["lightLevel"] = receivedData[i].lightLevel;
        obj["humidity"] = receivedData[i].humidity;
        obj["soilMoisture"] = receivedData[i].soilMoisture;
        obj["solarCurrent"] = receivedData[i].solarCurrent;
        obj["solarVoltage"] = receivedData[i].solarVoltage;
        obj["loadCurrent"] = receivedData[i].loadCurrent;
        obj["loadVoltage"] = receivedData[i].loadVoltage;
    }

    String jsonString;
    serializeJson(doc, jsonString);
    return jsonString;
}

/* Global Variables */

std::vector<ESP_NOW_Peer_Class> masters;

/* Callbacks */

// Callback called when an unknown peer sends a message
void register_new_master(const esp_now_recv_info_t *info, const uint8_t *data, int len, void *arg) {
  if (memcmp(info->des_addr, ESP_NOW.BROADCAST_ADDR, 6) == 0) {
    Serial.printf("Unknown peer " MACSTR " sent a broadcast message\n", MAC2STR(info->src_addr));
    Serial.println("Registering the peer as a master");

    ESP_NOW_Peer_Class new_master(info->src_addr, ESPNOW_WIFI_CHANNEL, WIFI_IF_STA, NULL);

    masters.push_back(new_master);
    if (!masters.back().add_peer()) {
      Serial.println("Failed to register the new master");
      return;
    }
  } else {
    // The slave will only receive broadcast messages
    log_v("Received a unicast message from " MACSTR, MAC2STR(info->src_addr));
    log_v("Igorning the message");
  }
}

/* Main */

void setup() {
  Serial.begin(115200);

  // Initialize the Wi-Fi module
  WiFi.mode(WIFI_STA);
  WiFi.setChannel(ESPNOW_WIFI_CHANNEL);
  while (!WiFi.STA.started()) {
    delay(100);
  }

  // // Connect to Wi-Fi (Modify with your SSID & PASSWORD if needed)
    WiFi.begin("TP-Link_E828", "29455898");  
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Connecting to Wi-Fi...");
    }

    // Print ESP32 IP Address
    Serial.print("Connected! ESP32 IP Address: ");
    Serial.println(WiFi.localIP());

  Serial.println("ESP-NOW Example - Broadcast Slave");
  Serial.println("Wi-Fi parameters:");
  Serial.println("  Mode: STA");
  Serial.println("  MAC Address: " + WiFi.macAddress());
  Serial.printf("  Channel: %d\n", ESPNOW_WIFI_CHANNEL);

  // Initialize the ESP-NOW protocol
  if (!ESP_NOW.begin()) {
    Serial.println("Failed to initialize ESP-NOW");
    Serial.println("Reeboting in 5 seconds...");
    delay(1000);
    ESP.restart();
  }

  // Register the new peer callback
  ESP_NOW.onNewPeer(register_new_master, NULL);

  Serial.println("Setup complete. Waiting for a master to broadcast a message...");

    // Web Server Routes
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send_P(200, "text/html", webpage);
    });

    server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", getDataJson());
    });

    server.begin();

}

void loop() {
    delay(1);

}
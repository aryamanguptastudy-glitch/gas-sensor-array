#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// -----------------------------------------
// 1. NETWORK & CLOUD CONFIGURATION
// -----------------------------------------
const char* ssid = "<YOUR_WIFI_SSID>";         // Inject your local Wi-Fi SSID
const char* password = "<YOUR_WIFI_PASSWORD>"; // Inject your local Wi-Fi Password

const char* mqtt_server = "<MQTT_SERVER_IP>";  // Your Oracle Cloud Public IP
const int mqtt_port = 1883;
const char* mqtt_topic = "ammonia/sensor";   // Must exactly match telegraf.conf

WiFiClient espClient;
PubSubClient client(espClient);

// -----------------------------------------
// 2. HARDWARE ARCHITECTURE
// -----------------------------------------
// Assuming 4 chemiresistors. Map these to your exact physical ESP32 ADC pins.
// Use ADC1 pins (32, 33, 34, 35, 36, 39) as ADC2 is disabled when Wi-Fi is active.
const int SENSOR_PIN_1 = 32;
const int SENSOR_PIN_2 = 33;
const int SENSOR_PIN_3 = 34;
const int SENSOR_PIN_4 = 35;

// Circuit constants
const float RL_VALUE = 100000.0; // 100k Ohms
const float V_REF = 3.3;         // ESP32 standard logic voltage
const float ADC_RES = 4095.0;    // 12-bit ADC resolution

// -----------------------------------------
// 3. SYSTEM FUNCTIONS
// -----------------------------------------
void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to network: ");
  Serial.println(ssid);

  // 1. Set strictly to Station Mode (disables internal Access Point)
  WiFi.mode(WIFI_STA);
  // 2. Erase the NVS cache and disconnect from ghost networks
  WiFi.disconnect(true, true); 
  delay(100);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected.");
  Serial.print("ESP32 IP address: ");
  Serial.println(WiFi.localIP());
}


void reconnect() {
  // Loop until reconnected to the Mosquitto broker
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection to Oracle Cloud...");
    
    // Generate a random client ID
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    
    // Attempt to connect (no username/password based on your mosquitto.conf)
    if (client.connect(clientId.c_str())) {
      Serial.println("CONNECTED.");
    } else {
      Serial.print("FAILED, rc=");
      Serial.print(client.state());
      Serial.println(" -> Retrying in 5 seconds");
      delay(5000);
    }
  }
}

float calculateCurrent(int pin) {
  int adc_value = analogRead(pin);
  // Calculate voltage drop across the 100k resistor
  float voltage = (adc_value / ADC_RES) * V_REF;
  // Calculate and return current (I = V/R) in Amperes
  return voltage / RL_VALUE; 
}

// -----------------------------------------
// 4. MAIN KERNEL
// -----------------------------------------
void setup() {
  Serial.begin(115200);
  
  // Enforce 12-bit ADC resolution explicitly
  analogReadResolution(12);
  
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
}

void loop() {
  // Ensure network persistence
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // Initialize JSON memory pool
  StaticJsonDocument<256> doc;

  // Execute ADC reads and populate the JSON object
  // The keys here exactly match the Grafana Flux query provided earlier
  doc["s1_current"] = calculateCurrent(SENSOR_PIN_1);
  doc["s2_current"] = calculateCurrent(SENSOR_PIN_2);
  doc["s3_current"] = calculateCurrent(SENSOR_PIN_3);
  doc["s4_current"] = calculateCurrent(SENSOR_PIN_4);

  // Serialize the document into a char array for transmission
  char jsonBuffer[256];
  serializeJson(doc, jsonBuffer);

  // Publish to Mosquitto
  client.publish(mqtt_topic, jsonBuffer);
  
  // Print to local Mac Serial Monitor (optional, for debugging before untethering)
  Serial.println(jsonBuffer);

  // Strict 1000ms delay to enforce 1Hz polling limit and protect the cloud server RAM
  delay(1000);
}

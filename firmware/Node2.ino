


//This Code was Tested.





#include <WiFi.h>
#include <PubSubClient.h>

// ========== Wi-Fi Credentials ==========
const char* ssid = "WIFI_SSID";       
const char* password = "WIFI_PASSWORD";

// ========== MQTT Broker ==========
const char* mqtt_server = "RASPBERRY_PI_IP";

// ========== Pin Definitions ==========
#define SOUND_SENSOR_PIN  36  
#define ALERT_LED_PIN     27 

// ========== Objects ==========
WiFiClient espClient;
PubSubClient client(espClient);

// ========== Variables ==========

unsigned long lastNoiseCheck = 0;
const unsigned long noiseInterval = 3000; // Check every 3 seconds
int noiseThreshold = 2500; //might need to adjust after testing
unsigned long alertStartTime = 0;
bool alertActive = false;
const unsigned long alertDuration = 30000; // 30 seconds


// ========== Setup ==========
void setup() {
  Serial.begin(115200);

  pinMode(SOUND_SENSOR_PIN, INPUT);
  pinMode(ALERT_LED_PIN, OUTPUT);

  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

// ========== Wi-Fi Setup ==========
void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
}

// ========== MQTT Reconnect ==========
void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("ESP32_Node2")) {
      Serial.println("connected");
      client.subscribe("shesecure/alerts");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

// ========== MQTT Callback ==========
void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(message);

  if (String(topic) == "shesecure/alerts") {
    handleMasterAlert(message);
  }
}

// ========== Handle Master Alert ==========
void handleMasterAlert(String alertType) {
  if (alertType == "PANIC" || alertType == "NOISE" || alertType == "ALCOHOL") {
    digitalWrite(ALERT_LED_PIN, HIGH);
    alertStartTime = millis();
    alertActive = true;
  }
}

void handleAlertTimeout() {
  if (alertActive && millis() - alertStartTime > alertDuration) {
    digitalWrite(ALERT_LED_PIN, LOW);
    alertActive = false;
  }
}

// ========== Main Loop ==========
void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  checkNoiseLevel();
  handelAlertTimeout();x
}

// ========== Check Noise Sensor ==========
void checkNoiseLevel() {
  if (millis() - lastNoiseCheck > noiseInterval) {
    lastNoiseCheck = millis();
    int noiseValue = analogRead(SOUND_SENSOR_PIN);

    Serial.print("Noise Sensor Value: ");
    Serial.println(noiseValue);

    if (noiseValue > noiseThreshold) {
      Serial.println("Loud Noise Detected!");
      client.publish("shesecure/noise", "Noise Detected");
    }
  }
}

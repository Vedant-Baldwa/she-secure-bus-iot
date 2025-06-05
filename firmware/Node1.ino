#include <Keypad.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <PubSubClient.h>


const char* ssid         = "Jarvis";
const char* password     = "20127600";
const char* mqtt_server  = "192.168.157.177";

WiFiClient    espClient;
PubSubClient  client(espClient);


LiquidCrystal_I2C lcd(0x27, 16, 2);

const byte ROWS = 4, COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {4, 5, 6, 7};
byte colPins[COLS] = {9, 8, 20, 21};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// —— Pins & Timing ——
#define BUZZER_PIN   19
#define LED_PIN      18
#define PANIC_PIN    10
#define ALCOHOL_PIN  1    

unsigned long lastAlcoholCheck = 0;
const    unsigned long alcoholInterval = 5000;

unsigned long alertStartTime  = 0;
const    unsigned long alertDuration  = 20000;  
bool alertActive = false;

// —— State Flags ——
bool lastPanicState = HIGH;
bool alcoholDetected = false;

void setup() {
  Serial.begin(9600);

  // GPIO setup
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);     digitalWrite(LED_PIN, LOW);
  pinMode(PANIC_PIN, INPUT_PULLUP);
  pinMode(ALCOHOL_PIN, INPUT);

  // LCD
  Wire.begin(22, 23);
  lcd.begin(16,2);
  lcd.backlight();
  lcd.print("SHE Secure Bus");
  delay(2000);
  lcd.clear();
  lcd.print("System Secure");

  // Wi-Fi + MQTT
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

void setup_wifi() {
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
}

void reconnect() {
  while (!client.connected()) {
    if (client.connect("ESP32_Node1")) {
      client.subscribe("shesecure/alerts");
      client.subscribe("shesecure/display_name");
      client.subscribe("shesecure/custom_lcd");
    } else {
      delay(2000);
    }
  }
}

void callback(char* topic, byte* payload, unsigned int len) {
  String msg;
  for (int i=0; i<len; i++) msg += (char)payload[i];
  if (String(topic) == "shesecure/alerts")        handleMasterAlert(msg);
  else if (String(topic) == "shesecure/display_name") handleName(msg);
  else if (String(topic) == "shesecure/custom_lcd")   handleCustom(msg);
}

void handleName(String name) {
  lcd.clear();
  if (name=="UNAUTHORIZED!") {
    lcd.print("Access Denied");
    digitalWrite(BUZZER_PIN, HIGH);
    delay(2000);
    digitalWrite(BUZZER_PIN, LOW);
  } else {
    lcd.print("Welcome:");
    lcd.setCursor(0,1);
    lcd.print(name);
    delay(2000);
  }
  lcd.clear();
  lcd.print("System Secure");
}

void handleCustom(String msg) {
  lcd.clear();
  lcd.setCursor(0,0);
  if (msg=="CLEAR") lcd.print("System Secure");
  else             lcd.print(msg);
}

void handleMasterAlert(String type) {
  if (type=="PANIC" || type=="NOISE") {
    digitalWrite(BUZZER_PIN, HIGH);
    digitalWrite(LED_PIN, HIGH);
    lcd.clear(); lcd.print("ALERT! "+type);
    alertActive = true;
    alertStartTime = millis();
  }
  else if (type=="ALCOHOL") {
    digitalWrite(LED_PIN, HIGH);
    lcd.clear(); lcd.print("ALERT! ALCOHOL");
    alertActive = true;               
    alertStartTime = millis();
  }
}

void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  checkKeypadInput();
  checkPanicButton();
  checkAlcoholSensor();
  checkAlertTimeout();
}

// —— Keypad Input ——
void checkKeypadInput() {
  static String idBuffer = "";
  static bool idSent = false;
  char key = keypad.getKey();
  if (!key) { idSent = false; return; }

  if (key=='*' && idBuffer.length()>0 && !idSent) {
    client.publish("shesecure/keypad_id", idBuffer.c_str());
    lcd.clear(); lcd.print("ID Sent!");
    idBuffer=""; idSent=true;
  }
  else if (key=='#') {
    idBuffer=""; lcd.clear(); lcd.print("ID Cleared");
    delay(10);
    lcd.print("system secured");
  }
  else if (!idSent) {
    idBuffer += key;
    lcd.clear(); lcd.print("ID: "+idBuffer);
  }
}

// —— Panic Button, edge-only ——
void checkPanicButton() {
  bool curr = digitalRead(PANIC_PIN);
  if (lastPanicState==HIGH && curr==LOW) {
    client.publish("shesecure/panic", "Panic Pressed");
  }
  lastPanicState = curr;
}

// —— Alcohol Sensor ——
void checkAlcoholSensor() {
  if (millis()-lastAlcoholCheck < alcoholInterval) return;
  lastAlcoholCheck = millis();

  int val = analogRead(ALCOHOL_PIN);

  if (val>2200 && !alcoholDetected) {
    alcoholDetected = true;
    client.publish("shesecure/alcohol", "Alcohol Detected");
  }
  else if (val<=2000) {
    alcoholDetected = false;
  }
}

// —— Alert timeout for ALL alerts ——
void checkAlertTimeout() {
  if (alertActive && millis()-alertStartTime > alertDuration) {
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(LED_PIN, LOW);
    lcd.clear(); lcd.print("System Secure");
    alertActive = false;
  }
}

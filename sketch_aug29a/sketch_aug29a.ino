#include "HX711.h"
#include <ESP32Servo.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "time.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ---------- Wi-Fi ----------
const char* ssid = "MMA";  
const char* password = "MMAMMAMMA";

// ---------- HiveMQ Cloud (MQTT over TLS) ----------
const char* mqttServer = "bf82ef604f6b4a4fb45679d1f7df39b6.s1.eu.hivemq.cloud";
const int mqttPort = 8883;
const char* mqttUser = "hivemq.webclient.1756480192613";
const char* mqttPassword = "PTI3f1Cq6bH?$lE!vs.2";

// Topics
const char* TOPIC_CMD = "pet/feeder/food/command"; // SETTINGS:MEALS=4;PORTION=50;WATER=10
const char* TOPIC_WEIGHT = "pet/feeder/food/weight";
const char* TOPIC_WATER  = "pet/feeder/water/level";


WiFiClientSecure espClient;
PubSubClient client(espClient);

// ---------- NTP ----------
const char *ntpServer = "time.google.com";
const long gmtOffset_sec = 10800;   // Egypt GMT+3 (adjust if needed)
const int daylightOffset_sec = 0;

// ---------- Pins ----------
#define LDR_PIN    34
#define BUZZER_PIN 27
#define DOUT       32
#define CLK        33
#define SERVO_PIN  25
#define PUMP_PIN   26
#define SENSOR_PIN 35
#define MAX_SENSOR_VALUE 4095

LiquidCrystal_I2C lcd(0x27, 16, 2);
HX711 scale;
Servo gateServo;


// Single authoritative configuration variables:
int   MEALS_PER_DAY  = 3;     // 1..10
float PORTION_WEIGHT = 40.0f; // grams (gate will close once measured weight >= this)
float LEVEL_THRESHOLD = 15.0f; // percent (water pump ON when below threshold)

// ---------- Runtime state ----------
unsigned long buzzerStart = 0;
bool buzzerActive = false;
bool gateClosed = false;

int ldrThreshold = 2000;
bool targetActive = false;
unsigned long activeStart = 0;
int currentSlot = 0;
int lastRunDay = -1;

// Up to 10 slots/day
int slotHour[10];
int slotMinute[10];
int slotSecond[10];

// Remember the last-used start time (in seconds since 00:00:00). Default = 0 (midnight)
int lastStartSeconds = 0;

// ---------- Helper: compute slots given a start time (in seconds) ----------
void computeSlotsFromStart(int startInSeconds) {
  if (MEALS_PER_DAY < 1) MEALS_PER_DAY = 1;
  if (MEALS_PER_DAY > 10) MEALS_PER_DAY = 10;

  // interval in seconds (even spacing)
  int interval = (24 * 3600) / MEALS_PER_DAY;

  Serial.printf("🔄 Computing %d slots from start %02d:%02d:%02d (startSec=%d), interval=%d s\n",
                MEALS_PER_DAY,
                startInSeconds / 3600, (startInSeconds % 3600) / 60, startInSeconds % 60,
                startInSeconds, interval);

  for (int i = 0; i < MEALS_PER_DAY; ++i) {
    int t = (startInSeconds + i * interval) % (24 * 3600);
    slotHour[i] = t / 3600;
    slotMinute[i] = (t % 3600) / 60;
    slotSecond[i] = t % 60;
  }
  // clear remaining slots
  for (int i = MEALS_PER_DAY; i < 10; ++i) {
    slotHour[i] = slotMinute[i] = slotSecond[i] = 0;
  }

  // make sure currentSlot valid
  if (currentSlot >= MEALS_PER_DAY) currentSlot = 0;

  Serial.println("✅ Slots:");
  for (int i = 0; i < MEALS_PER_DAY; ++i) {
    Serial.printf("   Slot[%d] = %02d:%02d:%02d\n", i, slotHour[i], slotMinute[i], slotSecond[i]);
  }
}

// ---------- parse SETTINGS payload ----------
void parseSettings(String payload) {
  // Expected: SETTINGS:MEALS=4;PORTION=50;WATER=10;FIRST_MEAL_TIME=02:30:11
  if (!payload.startsWith("SETTINGS:")) {
    Serial.println("parseSettings: payload does not start with SETTINGS:");
    return;
  }
  String s = payload;
  s.remove(0, 9); // remove "SETTINGS:"

  int start = 0;
  while (start < (int)s.length()) {
    int end = s.indexOf(';', start);
    if (end == -1) end = s.length();
    String pair = s.substring(start, end);
    int eq = pair.indexOf('=');
    if (eq > 0) {
      String key = pair.substring(0, eq);
      String value = pair.substring(eq + 1);

      key.trim(); value.trim();

      if (key == "MEALS") {
        int m = value.toInt();
        MEALS_PER_DAY = constrain(m, 1, 10);
        Serial.printf("🔧 MEALS set to %d\n", MEALS_PER_DAY);

      } else if (key == "PORTION") {
        float p = value.toFloat();
        PORTION_WEIGHT = constrain(p, 0.0f, 5000.0f);
        Serial.printf("🔧 PORTION set to %.2f g\n", PORTION_WEIGHT);

      } else if (key == "WATER") {
        float w = value.toFloat();
        LEVEL_THRESHOLD = constrain(w, 0.0f, 100.0f);
        Serial.printf("🔧 WATER threshold set to %.2f %%\n", LEVEL_THRESHOLD);

      } else if (key == "FIRST_MEAL_TIME") {
        int h, m, sec;
        if (sscanf(value.c_str(), "%d:%d:%d", &h, &m, &sec) == 3) {
          h = constrain(h, 0, 23);
          m = constrain(m, 0, 59);
          sec = constrain(sec, 0, 59);
          lastStartSeconds = h * 3600 + m * 60 + sec;
          Serial.printf("🔧 FIRST_MEAL_TIME set to %02d:%02d:%02d (%d sec)\n", h, m, sec, lastStartSeconds);
        } else {
          Serial.println("⚠ Invalid FIRST_MEAL_TIME format. Expected HH:MM:SS");
        }
      } else {
        Serial.printf("⚠ Unknown SETTINGS key: %s\n", key.c_str());
      }
    }
    start = end + 1;
  }

  // Recompute slots using lastStartSeconds (if user set a start before, use it; otherwise 0)
  computeSlotsFromStart(lastStartSeconds);
}

// ---------- MQTT callback ----------
void callback(char* topic, byte* payload, unsigned int length) {
  String msg;
  msg.reserve(length + 1);
  for (unsigned int i = 0; i < length; ++i) msg += (char)payload[i];
  Serial.printf("📩 Received on [%s]: %s\n", topic, msg.c_str());

  String t = String(topic);

  if (t == TOPIC_CMD) {
    // SETTINGS payload
    parseSettings(msg);
  } else {
    Serial.println("⚠ Unhandled topic");
  }
}

// ---------- MQTT connect helper ----------
void connectMQTT() {
  while (!client.connected()) {
    Serial.print("Connecting to HiveMQ Cloud...");
    String clientId = "ESP32Client-" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str(), mqttUser, mqttPassword)) {
      Serial.println("connected!");
      client.subscribe(TOPIC_CMD);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" retrying in 0.5s...");
      delay(500);
    }
  }
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  delay(50);

  //LCD 
  Wire.begin(21, 22); // SDA = 21, SCL = 22
  lcd.init();         // Initialize LCD
  lcd.backlight();    // Turn on backlight

  // Wi-Fi
  Serial.print("Connecting to Wi-Fi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(200); Serial.print("."); }
  Serial.print("\n✅ WiFi connected. IP: ");
  Serial.println(WiFi.localIP());
  delay(1000);

  // NTP
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("🔄 NTP configured (time.google.com).");

  // MQTT
  espClient.setInsecure(); // for testing only — accepts any cert
  client.setServer(mqttServer, mqttPort);
  client.setCallback(callback);

  // IO init
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  gateServo.attach(SERVO_PIN);
  gateServo.write(45); // open

  scale.begin(DOUT, CLK);
  scale.set_scale(420.0f); // adjust/calibrate to your scale
  scale.tare();

  pinMode(PUMP_PIN, OUTPUT);
  digitalWrite(PUMP_PIN, LOW);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  // initialize slots: default start = midnight (0)
  lastStartSeconds = 0;
  computeSlotsFromStart(lastStartSeconds);

  Serial.println("System Ready.");
}

// ---------- Loop ----------
void loop() {
  // MQTT connection maintenance
  if (!client.connected()) connectMQTT();
  client.loop();

  // Time read (5s timeout)
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 5000)) {
    Serial.println("❌ Failed to get time from NTP server (timeout).");
  } else {
    // day change detection (works even if we miss exact 00:00:00)
    if (lastRunDay == -1) {
      lastRunDay = timeinfo.tm_mday;
    } else if (timeinfo.tm_mday != lastRunDay) {
      currentSlot = 0;
      lastRunDay = timeinfo.tm_mday;
      Serial.println("🔄 New day detected — slot counter reset.");
    }

    // print time only when idle
    if (!targetActive) {
      if (currentSlot < MEALS_PER_DAY) {
        Serial.printf("⏰ Now %02d:%02d:%02d | Waiting Slot[%d] at %02d:%02d:%02d\n",
                      timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
                      currentSlot,
                      slotHour[currentSlot], slotMinute[currentSlot], slotSecond[currentSlot]);
      } else {
        Serial.println("⏸ All meals done for today.");
      }
    }

    // trigger slot if we still have slots left and not active
    if (currentSlot < MEALS_PER_DAY && !targetActive) {
      int nowSec = timeinfo.tm_hour * 3600 + timeinfo.tm_min * 60 + timeinfo.tm_sec;
      int slotSec = slotHour[currentSlot] * 3600 + slotMinute[currentSlot] * 60 + slotSecond[currentSlot];
      if (abs(nowSec - slotSec) <= 2) { // ±2s tolerance
        Serial.printf("🎯 Slot %d started at %02d:%02d:%02d\n", currentSlot,
                      timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        targetActive = true;
        activeStart = millis();
      }
    }

    // active slot logic (5 minutes)
    if (targetActive) {
      unsigned long elapsed = millis() - activeStart;
      if (elapsed <= 5UL * 60UL * 1000UL) { // 5 minutes
        if (scale.is_ready()) {
          float weight = scale.get_units(5)/2; // average 5 readings
          if (weight < 0) weight = 0;

          char weightMsg[32];
          snprintf(weightMsg, sizeof(weightMsg), "weight: %.2f", weight);
          client.publish(TOPIC_WEIGHT, weightMsg);
          Serial.printf("⚖ Weight: %.2f g (Target: %.2f g)\n", weight, PORTION_WEIGHT);

          if (weight >= PORTION_WEIGHT) {
            gateServo.write(0); // close gate
            gateClosed = true;
            Serial.println("Gate CLOSED");
            lcd.setCursor(0, 1);
            lcd.print("weight reached");

            if (!buzzerActive) {
              buzzerActive = true;
              buzzerStart = millis();
              digitalWrite(BUZZER_PIN, HIGH);
              Serial.println("Buzzer ON");
            }
          } else {
            gateServo.write(45); // open gate
            gateClosed = false;
            Serial.println("Gate OPENED");
            lcd.setCursor(0, 1);
            lcd.print("weight below");
          }
        }

        // buzzer auto-off conditions
        if (buzzerActive) {
          int ldrValue = analogRead(LDR_PIN);
          if ((millis() - buzzerStart >= 120000) || (ldrValue < ldrThreshold)) {
            buzzerActive = false;
            digitalWrite(BUZZER_PIN, LOW);
            Serial.println("Buzzer OFF");
          }
        }
      } else {
        // end of slot
        targetActive = false;
        Serial.printf("⏹ Slot %d finished\n", currentSlot);
        currentSlot++;
        if (currentSlot >= MEALS_PER_DAY) {
          Serial.println("✅ All meals done for today");
        }
      }
    }
  } // end getLocalTime

// Water sensor logic (always active, threshold is configurable)
  int raw = analogRead(SENSOR_PIN);
  float percent = (raw * 2 * 100.0) / MAX_SENSOR_VALUE;

  char waterMsg[32];
  snprintf(waterMsg, sizeof(waterMsg), "Level: %.2f", percent);
  client.publish(TOPIC_WATER, waterMsg);
  Serial.printf("RAW: %d   Level: %.2f%%\n", raw, percent);

  if (percent < LEVEL_THRESHOLD) {
  digitalWrite(PUMP_PIN, HIGH);  // Pump ON
  Serial.println("🚰 Pump ON (Water below threshold)");
  lcd.setCursor(0, 0);
  lcd.print("water low");
  } else {
  digitalWrite(PUMP_PIN, LOW);   // Pump OFF
  Serial.println("💧 Pump OFF (Water above threshold)");
  lcd.setCursor(0, 0);
  lcd.print("water high");
  }
  delay(1000);
  }
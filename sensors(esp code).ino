#include <ESP8266WiFi.h>
#include <Firebase_ESP_Client.h>
#include <DHT.h>

// ==== CONFIGURATION ====
#define WIFI_SSID "Srinjoy5G"
#define WIFI_PASSWORD "gotohell"
#define API_KEY "AIzaSyCII_qpWN7Xj0-qEYtIIQNzDmDoObywHoI"
#define DATABASE_URL "https://ledblink-f184b-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define USER_EMAIL "srinjoyt06@gmail.com"
#define USER_PASSWORD "00112233"

// Pin defs
#define TRIG_PIN 5       // D5 (GPIO14)
#define ECHO_PIN 4       // D8 (GPIO15)
#define SOIL_MOISTURE_PIN A0
#define DHTPIN 2          // D4 (GPIO2)
#define BUTTON_PIN 0      // GPIO0 (D3), push button pin

#define DHTTYPE DHT11

const int SOIL_DRY_RAW = 1023;
const int SOIL_WET_RAW = 400;

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

DHT dht(DHTPIN, DHTTYPE);

unsigned long prevMillis = 0;
bool lastButtonState = HIGH;
float newTankMaxCm = 30.0;   // Default tank empty height (max distance)
const float tankMinCm = 2.0; // Minimum tank height to consider full

void setup() {
  Serial.begin(115200);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  dht.begin();

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.println();
  Serial.printf("Connected with IP: %s\n", WiFi.localIP().toString().c_str());

  // Firebase config and auth
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  Firebase.reconnectNetwork(true);
  fbdo.setBSSLBufferSize(4096, 1024);
  fbdo.setResponseSize(2048);

  Firebase.begin(&config, &auth);
  Firebase.setDoubleDigits(2);
  config.timeout.serverResponse = 10000;
}

long readDistanceCM() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duration == 0) return -1;
  return (duration * 0.0343) / 2;
}

int readSoilMoisturePercent() {
  int raw = analogRead(SOIL_MOISTURE_PIN);
  if (raw > SOIL_DRY_RAW) raw = SOIL_DRY_RAW;
  if (raw < SOIL_WET_RAW) raw = SOIL_WET_RAW;
  float percent = ((float)(SOIL_DRY_RAW - raw) / (SOIL_DRY_RAW - SOIL_WET_RAW)) * 100.0;
  return (int)percent;
}

int calcWaterLevelPercent(long distCm) {
  if (distCm < 0) return 0;
  if (distCm >= newTankMaxCm) return 0;
  if (distCm <= tankMinCm) return 100;

  float tankDepth = newTankMaxCm - tankMinCm;
  float distanceFromMin = distCm - tankMinCm;
  float waterLevelPercent = 100.0 - ((distanceFromMin / tankDepth) * 100.0);
  if (waterLevelPercent < 0) waterLevelPercent = 0;
  if (waterLevelPercent > 100) waterLevelPercent = 100;
  return (int)waterLevelPercent;
}

void checkButtonPress() {
  bool buttonState = digitalRead(BUTTON_PIN);
  if (lastButtonState == HIGH && buttonState == LOW) {
    long dist = readDistanceCM();
    if (dist > 0) {
      newTankMaxCm = dist;
      Serial.printf("Button pressed. New tank empty height set to %.1f cm\n", newTankMaxCm);
      if (Firebase.RTDB.setFloat(&fbdo, "/sensors/tankEmptyHeight", newTankMaxCm)) {
        Serial.println("Calibration updated on Firebase.");
      } else {
        Serial.printf("Firebase write failed: %s\n", fbdo.errorReason().c_str());
      }
    } else {
      Serial.println("Invalid distance for calibration.");
    }
  }
  lastButtonState = buttonState;
}

void loop() {
  checkButtonPress();

  if (Firebase.ready() && (millis() - prevMillis > 2000 || prevMillis == 0)) {
    prevMillis = millis();

    long dist = readDistanceCM();
    int waterLevelPercent = calcWaterLevelPercent(dist);
    int soilMoisture = readSoilMoisturePercent();
    float humidity = dht.readHumidity();
    float temperature = dht.readTemperature();

    if (isnan(humidity) || isnan(temperature)) {
      Serial.println("Failed to read DHT sensor!");
    } else {
      Serial.printf("Distance: %ld cm, Water Level: %d%%, Soil: %d%%, Temp: %.2f C, Humidity: %.2f%%\n",
                    dist, waterLevelPercent, soilMoisture, temperature, humidity);

      Firebase.RTDB.setInt(&fbdo, "/sensors/waterLevelDistance", dist);
      Firebase.RTDB.setInt(&fbdo, "/sensors/waterLevelPercent", waterLevelPercent);
      Firebase.RTDB.setInt(&fbdo, "/sensors/moisture", soilMoisture);
      Firebase.RTDB.setFloat(&fbdo, "/sensors/temperature", temperature);
      Firebase.RTDB.setFloat(&fbdo, "/sensors/humidity", humidity);
    }
  }
}

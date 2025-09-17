#include <ESP8266WiFi.h>
#include <Firebase_ESP_Client.h>

// ==== CONFIGURATION ====
#define WIFI_SSID "Srinjoy5G"
#define WIFI_PASSWORD "gotohell"
#define API_KEY "AIzaSyCII_qpWN7Xj0-qEYtIIQNzDmDoObywHoI"
#define DATABASE_URL "https://ledblink-f184b-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define USER_EMAIL "srinjoyt06@gmail.com"
#define USER_PASSWORD "00112233"

// Motor pins
#define PUMP1_IN1 4       // D2 (GPIO4)
#define PUMP1_IN2 5       // D1 (GPIO5)
#define PUMP2_IN1 14      // D5 (GPIO14)
#define PUMP2_IN2 0       // D3 (GPIO0)

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

void pump1On() {
  digitalWrite(PUMP1_IN1, HIGH);
  digitalWrite(PUMP1_IN2, LOW);
}
void pump1Off() {
  digitalWrite(PUMP1_IN1, LOW);
  digitalWrite(PUMP1_IN2, LOW);
}
void pump2On() {
  digitalWrite(PUMP2_IN1, HIGH);
  digitalWrite(PUMP2_IN2, LOW);
}
void pump2Off() {
  digitalWrite(PUMP2_IN1, LOW);
  digitalWrite(PUMP2_IN2, LOW);
}

unsigned long prevMillis = 0;

void setup() {
  Serial.begin(115200);

  pinMode(PUMP1_IN1, OUTPUT);
  pinMode(PUMP1_IN2, OUTPUT);
  pinMode(PUMP2_IN1, OUTPUT);
  pinMode(PUMP2_IN2, OUTPUT);

  pump1Off();
  pump2Off();

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.println();
  Serial.printf("Connected with IP: %s\n", WiFi.localIP().toString().c_str());

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

void loop() {
  if (Firebase.ready() && (millis() - prevMillis > 2000 || prevMillis == 0)) {
    prevMillis = millis();

    int pump1State = 0;
    if (Firebase.RTDB.getInt(&fbdo, "/controls/pump1")) {
      pump1State = fbdo.intData();

      // Read water level percentage
      if (Firebase.RTDB.getInt(&fbdo, "/sensors/waterLevelPercent")) {
        int waterLevelPercent = fbdo.intData();
        if (waterLevelPercent > 80 && pump1State == 1) {
          Serial.println("Pump1 OFF: Water level above 80%");
          pump1Off();
          Firebase.RTDB.setBool(&fbdo, "/controls/pump1", false);
        } else if (pump1State == 1) {
          pump1On();
        } else {
          pump1Off();
        }
      } else {
        // Fall back to normal control if water level not available
        if (pump1State == 1) pump1On();
        else pump1Off();
      }
    } else {
      pump1Off();
    }


    int pump2State = 0;
    if (Firebase.RTDB.getInt(&fbdo, "/controls/pump2")) {
      pump2State = fbdo.intData();

      // Read soil moisture percentage
      if (Firebase.RTDB.getInt(&fbdo, "/sensors/moisture")) {
        int soilMoisturePercent = fbdo.intData();
        if (soilMoisturePercent > 70 && pump2State == 1) {
          Serial.println("Pump2 OFF: Soil moisture above 70%");
          pump2Off();
          Firebase.RTDB.setBool(&fbdo, "/controls/pump2", false);
        } else if (pump2State == 1) {
          pump2On();
        } else {
          pump2Off();
        }
      } else {
        // Fall back to normal control if soil moisture not available
        if (pump2State == 1) pump2On();
        else pump2Off();
      }
    } else {
      pump2Off();
    }
  }
}

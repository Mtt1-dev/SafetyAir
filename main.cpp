// ========== main.ino (Improved with Web App Config) ==========
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <LiquidCrystal_I2C.h>
#include <BluetoothSerial.h>
#include <DHT11.h>

#define LED_PIN     2
#define FAN_PIN     3
#define PIX_PIN     4
#define PUMP_PIN    5
#define MQ135_PIN   34
#define PWM_CH      0
#define PWM_FREQ    5000
#define PWM_RES     8
#define DHTPIN      18
#define DHTTYPE     DHT22
#define DHT_CHECK_INTERVAL 5000
#define NIGHT_HOUR  22
#define MORNING_HOUR 7
#define HUMIDITY_LOW 40

Adafruit_NeoPixel strip(1, PIX_PIN, NEO_GRB + NEO_KHZ800);
WebServer server(80);
BluetoothSerial SerialBT;
LiquidCrystal_I2C lcd(0x27, 16, 2);
DHT11 dht(DHTPIN, DHTTYPE);

String ssid = "YOUR_SSID";
String pass = "YOUR_PASS";
String waqiToken = "YOUR_WAQI_TOKEN";
String waqiCity = "shanghai";

unsigned long lastAQIFetch = 0;
unsigned long aqiInterval = 60000;
unsigned long lastBreathAnim = 0;
unsigned long breathInterval = 50;
unsigned long lastSensorRead = 0;
unsigned long sensorInterval = 1000;
unsigned long lastDHTCheck = 0;

float curTemp = 0, curHum = 0;
float temperature = 0, humidity = 0;
float dayNightBrightness = 1.0;

int localAQ = 0;
int cityAQ = 0;
bool pumpState = false;

void setup() {
  Serial.begin(115200);
  SerialBT.begin("ESP32_AIRMONITOR");

  pinMode(LED_PIN, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);
  pinMode(PUMP_PIN, OUTPUT);
  pinMode(MQ135_PIN, INPUT);

  ledcSetup(PWM_CH, PWM_FREQ, PWM_RES);
  ledcAttachPin(FAN_PIN, PWM_CH);

  strip.begin();
  strip.show();

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Booting...");

  dht.begin();

  connectToWiFi();
  setupRoutes();
  server.begin();
}

void loop() {
  server.handleClient();
  unsigned long now = millis();

  if (now - lastSensorRead >= sensorInterval) {
    lastSensorRead = now;
    readSensors();
  }

  if (now - lastAQIFetch >= aqiInterval) {
    lastAQIFetch = now;
    fetchCityAQI();
  }

  if (now - lastBreathAnim >= breathInterval) {
    lastBreathAnim = now;
    breathingDi();
  }

  updateTimeBasedBrightness();
  handleBTConfig();
}

void connectToWiFi() {
  WiFi.begin(ssid.c_str(), pass.c_str());
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 10000) delay(500);
}

void handleBTConfig() {
  if (SerialBT.available()) {
    String input = SerialBT.readStringUntil('\n');
    input.trim();
    if (input.startsWith("SSID:")) {
      ssid = input.substring(5);
      SerialBT.println("SSID set to: " + ssid);
    } else if (input.startsWith("PASS:")) {
      pass = input.substring(5);
      SerialBT.println("Password updated");
    } else if (input.startsWith("CITY:")) {
      waqiCity = input.substring(5);
      SerialBT.println("City set to: " + waqiCity);
    } else if (input == "REBOOT") {
      SerialBT.println("Rebooting to apply settings...");
      ESP.restart();
    } else {
      SerialBT.println("Invalid input. Use SSID:<name>, PASS:<password>, CITY:<city>, or REBOOT");
    }
  }
}

void readSensors() {
  int mq = analogRead(MQ135_PIN);
  float factor = (float)(mq - 2000) / 500.0;
  factor = constrain(factor, 0, 5);
  float speed = pow(1.5, factor);
  int pwm = constrain((int)(speed * 30), 0, 255);
  ledcWrite(PWM_CH, pwm);

  if (mq > 2000 && !pumpState) setPump(true);
  if (mq <= 2000 && pumpState) setPump(false);

  uint8_t r = min(255, (mq - 2000) / 2);
  uint8_t g = max(0, 255 - (mq - 2000) / 2);
  strip.setPixelColor(0, strip.Color(r, g, 0));
  strip.show();
  localAQ = mq;

  updateDHT();
  updateLCD(localAQ);
}

void setPump(bool st) {
  digitalWrite(PUMP_PIN, st);
  pumpState = st;
}

void fetchCityAQI() {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  String url = "http://api.waqi.info/feed/" + waqiCity + "/?token=" + waqiToken;
  http.begin(url);
  int code = http.GET();
  if (code == 200) {
    String payload = http.getString();
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, payload);
    cityAQ = doc["data"]["aqi"];
  }
  http.end();
}

void setupRoutes() {
  server.on("/", []() {
    server.send(200, "text/html", R"(
      <html>
        <head>
          <style>
            body {
              font-family: Arial, sans-serif;
              background-color: #f4f4f4;
              margin: 0;
              padding: 0;
              display: flex;
              justify-content: center;
              align-items: center;
              height: 100vh;
            }
            h1 {
              text-align: center;
              color: #333;
            }
            form {
              background-color: white;
              padding: 20px;
              border-radius: 8px;
              box-shadow: 0 4px 8px rgba(0, 0, 0, 0.1);
              width: 300px;
              display: flex;
              flex-direction: column;
            }
            input {
              margin-bottom: 10px;
              padding: 10px;
              font-size: 14px;
              border: 1px solid #ccc;
              border-radius: 4px;
            }
            button {
              background-color: #4CAF50;
              color: white;
              border: none;
              padding: 10px;
              border-radius: 4px;
              cursor: pointer;
              font-size: 16px;
            }
            button:hover {
              background-color: #45a049;
            }
          </style>
        </head>
        <body>
          <div>
            <h1>ESP32 Config</h1>
            <form method='POST' action='/config'>
              <input name='ssid' placeholder='SSID' required>
              <input name='pass' type='password' placeholder='Password' required>
              <input name='city' placeholder='City' required>
              <button type='submit'>Save</button>
            </form>
          </div>
        </body>
      </html>
    )"});
  server.on("/config", HTTP_POST, []() {
    if (server.hasArg("ssid")) ssid = server.arg("ssid");
    if (server.hasArg("pass")) pass = server.arg("pass");
    if (server.hasArg("city")) waqiCity = server.arg("city");
    server.send(200, "text/html", "<h2>Config saved. Rebooting...</h2>");
    delay(2000);
    ESP.restart();
  });
  server.on("/on", []() { digitalWrite(LED_PIN, HIGH); server.send(200, "text", "LED ON"); });
  server.on("/off", []() { digitalWrite(LED_PIN, LOW); server.send(200, "text", "LED OFF"); });
  server.on("/toggle", []() {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    server.send(200, "text", "LED TOGGLED");
  });
  server.on("/fanon", []() { ledcWrite(PWM_CH, 255); server.send(200, "text", "Fan MAX"); });
  server.on("/fanoff", []() { ledcWrite(PWM_CH, 0); server.send(200, "text", "Fan OFF"); });
  server.on("/color", []() {
    if (server.hasArg("r") && server.hasArg("g") && server.hasArg("b")) {
      uint8_t r = server.arg("r").toInt();
      uint8_t g = server.arg("g").toInt();
      uint8_t b = server.arg("b").toInt();
      strip.setPixelColor(0, strip.Color(r, g, b));
      strip.show();
      server.send(200, "text", "Color set");
    } else server.send(400, "text", "Missing RGB");
  });
  server.on("/data", []() {
    String json = "{";
    json += "\"localAQ\":" + String(localAQ) + ",";
    json += "\"cityAQ\":" + String(cityAQ) + ",";
    json += "\"temp\":" + String(curTemp) + ",";
    json += "\"humidity\":" + String(curHum) + "}";
    server.send(200, "application/json", json);
  });
  server.onNotFound([]() { server.send(404, "text", "Not found"); });
}

// Ahora debes mover el contenido de dht_time_functions.ino aquí abajo directamente,
// y eliminar cualquier duplicado o conflicto antes de compilar.

// ======== DHT11 SENSOR READINGS ======== //
void updateDHTReadings() {
  if (millis() - lastDHTCheck >= DHT_CHECK_INTERVAL) {
    lastDHTCheck = millis();
    
    // Read temperature and humidity from DHT11
    float newTemp = dht.readTemperature();
    float newHumidity = dht.readHumidity();
    
    // Check if readings are valid
    if (!isnan(newTemp) && !isnan(newHumidity)) {
      temperature = newTemp;
      humidity = newHumidity;
      
      Serial.print("Temperature: ");
      Serial.print(temperature);
      Serial.print("°C, Humidity: ");
      Serial.print(humidity);
      Serial.println("%");
      
      // Check if humidity is too low
      if (humidity < HUMIDITY_LOW) {
        Serial.println("Warning: Low humidity detected!");
      }
    }
  }
}

// ======== TIME-BASED BRIGHTNESS CONTROL ======== //
void updateTimeBasedBrightness() {
  static unsigned long lastTimeCheck = 0;
  const long timeCheckInterval = 60000; // Check time every minute
  
  if (millis() - lastTimeCheck >= timeCheckInterval) {
    lastTimeCheck = millis();
    
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      int currentHour = timeinfo.tm_hour;
      
      // Night time (8 PM to 8 AM): dim the lights
      if (currentHour >= NIGHT_HOUR || currentHour < MORNING_HOUR) {
        // Gradually transition to night mode if not already there
        dayNightBrightness = max(0.2, dayNightBrightness - 0.05);
      } else {
        // Gradually transition to day mode if not already there
        dayNightBrightness = min(1.0, dayNightBrightness + 0.05);
      }
    }
  }
}

// ======== UTILITY FUNCTIONS ======== //
void printLocalTime() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    Serial.println(&timeinfo, "Current time: %A, %B %d %Y %H:%M:%S");
  } else {
    Serial.println("Failed to obtain time");
  }
}

// ========== main.ino (WiFi Config Only Version) ==========
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include "driver/ledc.h"
#include <EEPROM.h>

// === Pin and device configuration ===
#define LED_PIN     2
#define FAN_PIN     3
#define PIX_PIN     4
#define PUMP_PIN    5
#define MQ135_PIN   34

// === LEDC (PWM) channel config ===
#define PWM_FREQ    5000
#define PWM_RES     8
uint8_t fanCh = 0;

// === DHT sensor configuration ===
#define DHTPIN      18
#define DHTTYPE     DHT11
#define DHT_CHECK_INTERVAL 5000

// === Brightness control hours ===
#define NIGHT_HOUR  22
#define MORNING_HOUR 8

#define HUMIDITY_LOW 40
#define EEPROM_SIZE 512

// === Global instances ===
Adafruit_NeoPixel strip(1, PIX_PIN, NEO_GRB + NEO_KHZ800);
WebServer server(80);
LiquidCrystal_I2C lcd(0x27, 16, 2);
DHT dht(DHTPIN, DHTTYPE);

// === Network & AQI settings ===
String ssid = "YOUR_SSID";
String pass = "YOUR_PASS";
String waqiToken = "YOUR_WAQI_TOKEN";
String waqiCity = "shanghai";
bool apMode = false;

// === Timers ===
unsigned long lastAQIFetch = 0;
unsigned long aqiInterval = 60000;
unsigned long lastBreathAnim = 0;
unsigned long breathInterval = 50;
unsigned long lastSensorRead = 0;
unsigned long sensorInterval = 1000;
unsigned long lastDHTCheck = 0;

// === Sensor values ===
float curTemp = 0, curHum = 0;
float temperature = 0, humidity = 0;
float dayNightBrightness = 1.0;

int localAQ = 0;
int cityAQ = 0;
bool pumpState = false;

void setup() {
  Serial.begin(115200);
  Serial.println("Starting SafetyAir Monitor");
  
  // Initialize EEPROM for settings storage
  EEPROM.begin(EEPROM_SIZE);
  
  // Load settings from EEPROM
  loadSettings();

  pinMode(LED_PIN, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);
  pinMode(PUMP_PIN, OUTPUT);
  pinMode(MQ135_PIN, INPUT);

  // Use new LEDC API: sets PWM channel for pin
  ledcAttach(FAN_PIN, PWM_FREQ, PWM_RES);

  strip.begin();
  strip.show(); // Turn off all LEDs initially

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Booting...");

  dht.begin();
  
  // Try to connect to WiFi, if fails, start AP mode
  if (!connectToWiFi()) {
    startAPMode();
  }
  
  setupRoutes();
  server.begin();
  
  Serial.println("Setup complete");
}

void loop() {
  server.handleClient();
  unsigned long now = millis();

  // Sensor readings every second
  if (now - lastSensorRead >= sensorInterval) {
    lastSensorRead = now;
    readSensors();
  }

  // Fetch AQI from web every minute (only if in station mode)
  if (!apMode && now - lastAQIFetch >= aqiInterval) {
    lastAQIFetch = now;
    fetchCityAQI();
  }                           

  // Run breathing LED animation
  if (now - lastBreathAnim >= breathInterval) {
    lastBreathAnim = now;
    breathingDi();
  }

  updateTimeBasedBrightness();
}

// === Load settings from EEPROM ===
void loadSettings() {
  // Read settings from EEPROM if they exist
  if (EEPROM.read(0) == 'C') { // Check if EEPROM has been initialized
    int addr = 1;
    
    // Read SSID
    int ssidLen = EEPROM.read(addr++);
    char ssidBuf[33] = {0};
    for (int i = 0; i < ssidLen && i < 32; i++) {
      ssidBuf[i] = EEPROM.read(addr++);
    }
    ssid = String(ssidBuf);
    
    // Read password
    int passLen = EEPROM.read(addr++);
    char passBuf[65] = {0};
    for (int i = 0; i < passLen && i < 64; i++) {
      passBuf[i] = EEPROM.read(addr++);
    }
    pass = String(passBuf);
    
    // Read city
    int cityLen = EEPROM.read(addr++);
    char cityBuf[33] = {0};
    for (int i = 0; i < cityLen && i < 32; i++) {
      cityBuf[i] = EEPROM.read(addr++);
    }
    waqiCity = String(cityBuf);
    
    Serial.println("Settings loaded from EEPROM");
  } else {
    Serial.println("No settings found in EEPROM, using defaults");
  }
}

// === Save settings to EEPROM ===
void saveSettings() {
  int addr = 0;
  
  // Mark EEPROM as initialized
  EEPROM.write(addr++, 'C');
  
  // Write SSID
  EEPROM.write(addr++, ssid.length());
  for (int i = 0; i < ssid.length(); i++) {
    EEPROM.write(addr++, ssid[i]);
  }
  
  // Write password
  EEPROM.write(addr++, pass.length());
  for (int i = 0; i < pass.length(); i++) {
    EEPROM.write(addr++, pass[i]);
  }
  
  // Write city
  EEPROM.write(addr++, waqiCity.length());
  for (int i = 0; i < waqiCity.length(); i++) {
    EEPROM.write(addr++, waqiCity[i]);
  }
  
  EEPROM.commit();
  Serial.println("Settings saved to EEPROM");
}

// === Connect to WiFi network ===
bool connectToWiFi() {
  Serial.println("Connecting to WiFi: " + ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 10000) {
    delay(500);
    Serial.print(".");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to WiFi");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    return true;
  } else {
    Serial.println("\nFailed to connect to WiFi");
    return false;
  }
}

// === Start AP Mode for configuration ===
void startAPMode() {
  Serial.println("Starting AP Mode");
  apMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP("SafetyAir-Config", "password");
  
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("AP: SafetyAir-Config");
  lcd.setCursor(0, 1);
  lcd.print("IP: ");
  lcd.print(IP);
}

// === Read sensors and react accordingly ===
void readSensors() {
  int mq = analogRead(MQ135_PIN);
  float factor = (float)(mq - 2000) / 500.0;
  factor = constrain(factor, 0, 5);
  float speed = pow(1.5, factor);
  int pwm = constrain((int)(speed * 30), 0, 255);
  ledcWrite(fanCh, pwm);

  if (mq > 2000 && !pumpState) setPump(true);
  if (mq <= 2000 && pumpState) setPump(false);

  // Set RGB LED color based on air quality
  uint8_t r = min(255, (mq - 2000) / 2);
  uint8_t g = max(0, 255 - (mq - 2000) / 2);
  strip.setPixelColor(0, strip.Color(r, g, 0));
  strip.show();
  localAQ = mq;

  updateDHTReadings();
  updateLCD(localAQ);
}

// === Set pump ON or OFF ===
void setPump(bool st) {
  digitalWrite(PUMP_PIN, st);
  pumpState = st;
}

// === Fetch city AQI from WAQI API ===
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

// === Breathing LED animation on LED_PIN ===
void breathingDi() {
  static int val = 0;
  static int dir = 5;
  val += dir;
  if (val >= 255 || val <= 0) dir = -dir;
  analogWrite(LED_PIN, val);
}

// === Update LCD with AQI and temp ===
void updateLCD(int aq) {
  if (apMode) return; // Don't update LCD in AP mode
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Local AQ: ");
  lcd.print(aq);
  lcd.setCursor(0, 1);
  lcd.print("Temp: ");
  lcd.print(temperature);
  lcd.print((char)223);
  lcd.print("C");
}

// === Set up web routes ===
void setupRoutes() {
  // Main page
  server.on("/", HTTP_GET, []() {
    if (apMode) {
      // Configuration page in AP mode
      String html = "<html><head><title>SafetyAir Config</title>";
      html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
      html += "<style>body{font-family:Arial;margin:0;padding:20px;} input,button{width:100%;padding:10px;margin:8px 0;} .container{max-width:400px;margin:0 auto;}</style>";
      html += "</head><body><div class='container'>";
      html += "<h1>SafetyAir Configuration</h1>";
      html += "<form action='/save' method='post'>";
      html += "WiFi SSID:<br><input type='text' name='ssid' value='" + ssid + "'><br>";
      html += "WiFi Password:<br><input type='password' name='pass' value='" + pass + "'><br>";
      html += "City for AQI:<br><input type='text' name='city' value='" + waqiCity + "'><br>";
      html += "<button type='submit'>Save and Restart</button>";
      html += "</form></div></body></html>";
      server.send(200, "text/html", html);
    } else {
      // Dashboard in station mode
      String html = "<html><head><title>SafetyAir Dashboard</title>";
      html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
      html += "<meta http-equiv='refresh' content='10'>";
      html += "<style>body{font-family:Arial;margin:0;padding:20px;} .container{max-width:600px;margin:0 auto;} .card{background:#f0f0f0;padding:15px;margin:10px 0;border-radius:5px;}</style>";
      html += "</head><body><div class='container'>";
      html += "<h1>SafetyAir Dashboard</h1>";
      html += "<div class='card'><h2>Local Air Quality</h2><p>Reading: " + String(localAQ) + "</p></div>";
      html += "<div class='card'><h2>City Air Quality</h2><p>AQI: " + String(cityAQ) + "</p></div>";
      html += "<div class='card'><h2>Temperature & Humidity</h2><p>Temperature: " + String(temperature) + "°C</p><p>Humidity: " + String(humidity) + "%</p></div>";
      html += "<p><a href='/config'>Configuration</a></p>";
      html += "</div></body></html>";
      server.send(200, "text/html", html);
    }
  });
  
  // Configuration page in station mode
  server.on("/config", HTTP_GET, []() {
    String html = "<html><head><title>SafetyAir Config</title>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>body{font-family:Arial;margin:0;padding:20px;} input,button{width:100%;padding:10px;margin:8px 0;} .container{max-width:400px;margin:0 auto;}</style>";
    html += "</head><body><div class='container'>";
    html += "<h1>SafetyAir Configuration</h1>";
    html += "<form action='/save' method='post'>";
    html += "WiFi SSID:<br><input type='text' name='ssid' value='" + ssid + "'><br>";
    html += "WiFi Password:<br><input type='password' name='pass' value='" + pass + "'><br>";
    html += "City for AQI:<br><input type='text' name='city' value='" + waqiCity + "'><br>";
    html += "<button type='submit'>Save and Restart</button>";
    html += "</form>";
    html += "<p><a href='/'>Back to Dashboard</a></p>";
    html += "</div></body></html>";
    server.send(200, "text/html", html);
  });
  
  // Save configuration
  server.on("/save", HTTP_POST, []() {
    ssid = server.arg("ssid");
    pass = server.arg("pass");
    waqiCity = server.arg("city");
    
    saveSettings();
    
    String html = "<html><head><title>Configuration Saved</title>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>body{font-family:Arial;margin:0;padding:20px;text-align:center;} .container{max-width:400px;margin:0 auto;}</style>";
    html += "<meta http-equiv='refresh' content='5;url=/'>";
    html += "</head><body><div class='container'>";
    html += "<h1>Configuration Saved</h1>";
    html += "<p>Your settings have been saved. Device will restart in 5 seconds.</p>";
    html += "</div></body></html>";
    server.send(200, "text/html", html);
    
    delay(1000);
    ESP.restart();
  });
}

// === Periodically update DHT readings with improved error handling ===
void updateDHTReadings() {
  if (millis() - lastDHTCheck >= DHT_CHECK_INTERVAL) {
    lastDHTCheck = millis();
    float newTemp = dht.readTemperature();
    float newHumidity = dht.readHumidity();
    if (!isnan(newTemp) && !isnan(newHumidity)) {
      temperature = newTemp;
      humidity = newHumidity;
      Serial.print("Temperature: ");
      Serial.print(temperature);
      Serial.print("°C, Humidity: ");
      Serial.print(humidity);
      Serial.println("%");
      if (humidity < HUMIDITY_LOW) Serial.println("Warning: Low humidity detected!");
    }
  }
}

// === Adjust brightness based on time with gradual changes ===
void updateTimeBasedBrightness() {
  static unsigned long lastTimeCheck = 0;
  const long timeCheckInterval = 60000;
  if (millis() - lastTimeCheck >= timeCheckInterval) {
    lastTimeCheck = millis();
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      int currentHour = timeinfo.tm_hour;
      if (currentHour >= NIGHT_HOUR || currentHour < MORNING_HOUR) {
        dayNightBrightness = max(0.2, dayNightBrightness - 0.05);
      } else {
        dayNightBrightness = min(1.0, dayNightBrightness + 0.05);
      }
    }
  }
}

// === Print local time utility function ===
void printLocalTime() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    Serial.println(&timeinfo, "Current time: %A, %B %d %Y %H:%M:%S");
  } else {
    Serial.println("Failed to obtain time");
  }
}

#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>

// ======== CONFIGURATION ======== //
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* waqiToken = "YOUR_WAQI_TOKEN";
const char* waqiCity = "shanghai";

// ======== PIN DEFINITIONS ======== //
#define LED_PIN 2
#define FAN_PIN 3
#define PUMP_PIN 5
#define NEOPIXEL_PIN 4
#define MQ135_PIN 34

// ======== GLOBAL OBJECTS ======== //
WebServer server(80);
Adafruit_NeoPixel neoPixel(1, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// ======== AIR QUALITY CONTROL ======== //
const int AIR_THRESHOLD = 1500;  // Calibrate this value
const int PWM_CHANNEL = 0;       // ESP32 has 16 PWM channels (0-15)
const int PWM_FREQ = 5000;       // 5kHz frequency
const int PWM_RESOLUTION = 8;    // 8-bit resolution (0-255)

// ======== COLOR ANIMATION ======== //
float currentR = 0, currentG = 255, currentB = 0;  // Start with green
uint8_t targetR = 0, targetG = 255, targetB = 0;
float brightness = 1.0;
bool breathingDirection = true;

void setup() {
  Serial.begin(115200);
  
  // Initialize hardware
  pinMode(LED_PIN, OUTPUT);
  pinMode(PUMP_PIN, OUTPUT);
  
  // PWM setup for fan control
  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RESOLUTION);
  ledcAttachPin(FAN_PIN, PWM_CHANNEL);
  
  // NeoPixel setup
  neoPixel.begin();
  neoPixel.show();

  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected! IP: " + WiFi.localIP());

  // Server endpoints
  server.on("/on", HTTP_GET, []() {
    digitalWrite(LED_PIN, HIGH);
    server.send(200, "text/plain", "LED ON");
  });

  server.on("/off", HTTP_GET, []() {
    digitalWrite(LED_PIN, LOW);
    server.send(200, "text/plain", "LED OFF");
  });

  server.on("/aqi", HTTP_GET, handleAQIRequest);
  server.onNotFound(handleNotFound);
  
  server.begin();
}

void loop() {
  server.handleClient();
  updateAirSystem();
  updateNeoPixel();
}

// ======== AIR QUALITY SYSTEM ======== //
void updateAirSystem() {
  static unsigned long lastCheck = 0;
  const long interval = 1000;  // 1 second interval
  
  if (millis() - lastCheck >= interval) {
    lastCheck = millis();
    
    int airValue = analogRead(MQ135_PIN);
    
    if (airValue > AIR_THRESHOLD) {
      // Exponential fan speed calculation
      float pollutionRatio = (airValue - AIR_THRESHOLD) / 500.0;
      int pwmValue = min(255, (int)(30 * pow(1.8, pollutionRatio)));
      ledcWrite(PWM_CHANNEL, pwmValue);
      digitalWrite(PUMP_PIN, HIGH);

      // Set target color (red intensity increases with pollution)
      targetR = min(255, (airValue - AIR_THRESHOLD) / 2);
      targetG = max(0, 255 - (airValue - AIR_THRESHOLD) / 2);
      targetB = 0;
    } else {
      ledcWrite(PWM_CHANNEL, 0);
      digitalWrite(PUMP_PIN, LOW);
      targetR = 0;
      targetG = 255;
      targetB = 0;
    }
  }
}

// ======== NEO PIXEL ANIMATION ======== //
void updateNeoPixel() {
  static unsigned long lastUpdate = 0;
  const int animationSpeed = 30;  // Lower = faster
  
  if (millis() - lastUpdate >= animationSpeed) {
    lastUpdate = millis();

    // Smooth color transition
    currentR += (targetR - currentR) * 0.15;
    currentG += (targetG - currentG) * 0.15;
    currentB += (targetB - currentB) * 0.15;

    // Breathing effect
    if (breathingDirection) {
      brightness += 0.015;
      if (brightness >= 1.0) breathingDirection = false;
    } else {
      brightness -= 0.015;
      if (brightness <= 0.4) breathingDirection = true;
    }

    // Apply brightness and update
    neoPixel.setPixelColor(0, neoPixel.Color(
      currentR * brightness,
      currentG * brightness,
      currentB * brightness
    ));
    neoPixel.show();
  }
}

// ======== WAQI API HANDLER ======== //
void handleAQIRequest() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = "http://api.waqi.info/feed/" + String(waqiCity) + "/?token=" + String(waqiToken);
    
    http.begin(url);
    int httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      DynamicJsonDocument doc(1024);
      deserializeJson(doc, payload);
      
      int aqi = doc["data"]["aqi"];
      server.send(200, "text/plain", "Current AQI: " + String(aqi));
    } else {
      server.send(500, "text/plain", "API Error");
    }
    http.end();
  } else {
    server.send(500, "text/plain", "WiFi Disconnected");
  }
}

void handleNotFound() {
  server.send(404, "text/plain", "Endpoint not found");
}
  

#include <Wire.h>
#include <WiFiClient.h>
#include <WiFiServer.h>
#include <WebServer.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <time.h>
#include <ArduinoOTA.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define LED_BUILTIN 2

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// WiFi config
const char* ssid = "TELUS7450";
const char* password = "amoking2";
WiFiServer tcpServer(23);  // data logger
WebServer webServer(80);   // webpage
String logBuffer = "";
String latestData = "";
WiFiClient client;
IPAddress local_IP(192, 168, 1, 188);  // the fixed IP
IPAddress gateway(192, 168, 1, 254);   // router’s IP
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8);
IPAddress secondaryDNS(8, 8, 4, 4);

// capacitive moisture config
const int moisturePin = 35;
const int dryValue = 3900;
const int wetValue = 1800;

void handleRoot() {
  String html = "<html><head><meta http-equiv='refresh' content='2'></head><body>";
  html += "<h2>Latest</h2><pre>" + latestData + "</pre>";
  html += "<h2>Log</h2><pre>" + logBuffer + "</pre>";
  html += "</body></html>";
  webServer.send(200, "text/html", html);
}

void i2cScan() {
  Serial.println("\n===== I2C Scanner =====");
  Serial.println("Scanning for OLED...");

  byte error, address;
  int nDevices = 0;

  for (address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0) {
      Serial.print("I2C device found at address 0x");
      if (address < 16) Serial.print("0");
      Serial.print(address, HEX);
      Serial.println("  ← OLED monitor");
      nDevices++;
    } else if (error == 4) {
      Serial.print("Unknown error at address 0x");
      if (address < 16) Serial.print("0");
      Serial.println(address, HEX);
    }
  }

  if (nDevices == 0) {
    Serial.println("No I2C devices found");
  } else {
    Serial.println("Scan complete.");
  }
  Serial.println("=======================\n");
}

void setup() {
  Serial.begin(115200);
  delay(3000);

  Serial.println("Setup start...");

  Wire.begin(21, 22);
  i2cScan();

  // ==== initilize OLED ====
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
      Serial.println("OLED failed");
      for (;;)
        ;
    }
  }

  // ==== initialize built-in LED light ====
  pinMode(LED_BUILTIN, OUTPUT);

  // ==== connecting to WiFi ====
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.println("Connecting to WiFi...");
  display.display();

  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.println("Static IP configuration failed");
  }

  Serial.print("Connecting to WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {  // wait up to ~15 seconds
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    display.clearDisplay();
    display.setTextSize(2);
    display.println("WiFi connected");
    display.display();

    webServer.on("/", handleRoot);
    webServer.begin();
    tcpServer.begin();
    Serial.println("Open http://" + WiFi.localIP().toString());
  } else {
    Serial.println("\nFailed to connect");
    Serial.print("Status: ");
    Serial.println(WiFi.status());
  }

  ArduinoOTA.setPassword("Amoking2_");
  ArduinoOTA.begin();

  struct tm timeinfo;
  configTime(-7 * 3600, 0, "pool.ntp.org");
  while (!getLocalTime(&timeinfo)) {
    Serial.println("Waiting for time...");
    delay(1000);
  }
  Serial.println("Time synced!");
}

void loop() {
  ArduinoOTA.handle();
  webServer.handleClient();
  digitalWrite(LED_BUILTIN, HIGH);
  delay(100);
  digitalWrite(LED_BUILTIN, LOW);

  // Accept new client if none is connected
  if (!client || !client.connected()) {
    client = tcpServer.accept();
  }

  // get mositure
  int raw = analogRead(moisturePin);
  int percent = map(raw, dryValue, wetValue, 0, 100);
  percent = constrain(percent, 0, 100);

  // get time
  struct tm timeinfo;
  char timeStr[20];
  if (getLocalTime(&timeinfo)) {
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
  }

  char line[64];
  snprintf(line, sizeof(line), "%s, %d, %d\n", timeStr, raw, percent);
  // Serial & client output
  if (client && client.connected())
    client.printf(line);

  Serial.print(line);

  // webpage output
  latestData = line;
  logBuffer += line;
  if (logBuffer.length() > 1500) {
    logBuffer.remove(0, 500);  // keep buffer from growing forever
  }

  // show moisture and time on OLED
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Moisture");

  display.setTextSize(2);
  display.setCursor(0, 16);
  display.print(raw);
  display.print("; ");
  display.print(percent);
  display.print("%");

  display.setTextSize(1);
  display.setCursor(0, 40);
  display.println("Time");

  display.setTextSize(2);
  display.setCursor(0, 50);
  display.println(timeStr);

  display.display();

  delay(5000);
}

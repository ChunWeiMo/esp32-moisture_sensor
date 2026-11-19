#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiServer.h>
#include <Wire.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define LED_BUILTIN 2

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// WiFi config
const char* ssid = "FF417-mini_PC";
const char* password = "FF417168";
WiFiServer tcpServer(23);  // data logger
WebServer webServer(80);   // webpage
String logBuffer = "";
String latestData = "";
WiFiClient clientToMiniPC;
WiFiClient incomingClient;
const char* mini_PC_ip = "192.168.137.2";
int mini_PC_port = 5000;

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
            for (;;);
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

    if (clientToMiniPC.connect(mini_PC_ip, mini_PC_port)) {
        String timeFromPC = clientToMiniPC.readStringUntil('\n');
        Serial.println("Received: " + timeFromPC);
        clientToMiniPC.stop();

        struct tm timeinfo;
        int year, month, day, hour, min, sec;

        if (sscanf(timeFromPC.c_str(), "%d-%d-%d %d:%d:%d",
                   &year, &month, &day, &hour, &min, &sec) == 6) {
            timeinfo.tm_year = year - 1900;
            timeinfo.tm_mon = month - 1;
            timeinfo.tm_mday = day;
            timeinfo.tm_hour = hour;
            timeinfo.tm_min = min;
            timeinfo.tm_sec = sec;

            time_t t = mktime(&timeinfo);
            struct timeval tv = {.tv_sec = t};
            settimeofday(&tv, NULL);

            Serial.println("Time set OK");
        } else {
            Serial.println("Parse failed");
        }
    } else {
        Serial.println("Connect failed");
    }

    struct tm timeinfo;
    char timeStr[20];
    if (getLocalTime(&timeinfo)) {
        strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
        Serial.println(timeStr);
    }
}

void loop() {
    ArduinoOTA.handle();
    webServer.handleClient();
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
    digitalWrite(LED_BUILTIN, LOW);

    // Accept new client if none is connected
    if (!incomingClient || !incomingClient.connected()) {
        incomingClient = tcpServer.accept();
    }

    // get mositure
    int raw = analogRead(moisturePin);
    int percent = map(raw, dryValue, wetValue, 0, 100);
    percent = constrain(percent, 0, 100);

    // get time
    struct tm timeinfo;
    char timeStr[20];
    if (getLocalTime(&timeinfo)) {
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
    }

    char line[64];
    snprintf(line, sizeof(line), "%s, %d, %d\n", timeStr, raw, percent);
    // Serial & client output
    if (incomingClient && incomingClient.connected())
        incomingClient.printf(line);

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
    display.println(timeStr + 11);

    display.display();

    delay(5000);
}

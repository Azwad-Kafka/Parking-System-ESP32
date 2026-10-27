#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include "time.h"

#include<string>

// --- WIFI SETTINGS ---
const char* ssid = "ID";
const char* password = "PASSWORD(PlaceHolder)";

// --- STATIC IP CONFIGURATION ---
// --- STATIC IP CONFIGURATION (Iphone Hotspot for Static IP)
IPAddress local_IP(172, 20, 10, 5);    // Assigned .5 for the ESP32 (Laptop is .6)
IPAddress gateway(172, 20, 10, 1);     // iPhone's internal IP
IPAddress subnet(255, 255, 255, 240);  // Critical for subnet connectivity.
IPAddress primaryDNS(172, 20, 10, 1);  // Using iPhone as DNS or 8.8.8.8

const char* www_username = "Admin";
const char* www_password = "ESU2026";

// --- PIN DEFINITIONS (Updated for your wiring) ---
const int internalLed = 2;
const int trigPin = 5;   // Changed from 14
const int echoPin = 18;  // Changed from 32

// Time & Thresholds
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -18000;
const int   daylightOffset_sec = 3600;
const float threshold = 8.0;

bool monitoringActive = false;
float distance = 0;
bool lastOccupiedState = false;

WebServer server(80);

// --- HELPER FUNCTIONS ---
String getTime() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)) return "Time Error";
  char buf[50];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buf);
}

void logEntry(String state) {
  File file = LittleFS.open("/log.csv", FILE_APPEND);
  if(!file) {
    Serial.println("Failed to open log file");
    return;
  }
  file.println(getTime() + "," + state + "," + String(distance));
  file.close();
}

void handleData() {
  String json = "{";
  json += "\"dist\":" + String(distance) + ",";
  json += "\"active\":" + String(monitoringActive ? "true" : "false") + ",";
  json += "\"disk\":\"" + String(LittleFS.usedBytes()/1024) + " KB / " + String(LittleFS.totalBytes()/1024) + " KB\"";
  json += "}";
  server.send(200, "application/json", json);
}

// --- HTML UI ---
String getHTML() {
  String html = "<!DOCTYPE html><html><head><title>Parking Dashboard</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "body{font-family:sans-serif;background:#121212;color:white;text-align:center;}";
  html += ".card{background:#1e1e1e;padding:20px;border-radius:15px;margin-top:20px;width:320px;display:inline-block;}";
  html += ".gauge-cont{background:#333;height:30px;border-radius:15px;margin:20px 0;overflow:hidden;}";
  html += "#gauge{background:#00e676;height:100%;width:0%;transition:width 0.2s;}";
  html += ".btn{padding:12px;border:none;border-radius:5px;color:white;margin:5px;text-decoration:none;display:inline-block;}";
  html += ".btn-start{background:#4caf50;} .btn-stop{background:#f44336;} .btn-blue{background:#2196f3;}";
  html += "#status{font-size:24px;margin:10px;}";
  html += "</style>";

  html += "<script>setInterval(function(){";
  html += "fetch('/data').then(r=>r.json()).then(d=>{";
  html += "document.getElementById('distText').innerText=d.dist.toFixed(1)+' cm';";
  html += "document.getElementById('diskText').innerText='Disk: '+d.disk;";
  html += "let g=document.getElementById('gauge');";
  html += "let s=document.getElementById('status');";
  html += "let p=Math.min(100,Math.max(0,(d.dist/50)*100));";
  html += "g.style.width=p+'%';";
  html += "if(d.dist<=" + String(threshold) + " && d.dist>0){";
  html += "s.innerText='OCCUPIED';s.style.color='#ff5252';g.style.background='#ff5252';}";
  html += "else{";
  html += "s.innerText='VACANT';s.style.color='#00e676';g.style.background='#00e676';}";
  html += "});},500);</script>";

  html += "</head><body>";
  html += "<div class='card'><h2>Parking Space #1</h2>";
  html += "<div id='status'>---</div>";
  html += "<h1 id='distText'>0.0 cm</h1>";
  html += "<div class='gauge-cont'><div id='gauge'></div></div>";
  html += "<a href='/start' class='btn btn-start'>START</a>";
  html += "<a href='/stop' class='btn btn-stop'>STOP</a><br>";
  html += "<a href='/download' class='btn btn-blue'>DOWNLOAD LOG</a>";
  html += "<div id='diskText'>Loading...</div>";
  html += "</div></body></html>";

  return html;
}

void WiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    Serial.println("WiFi lost. Reconnecting...");
    WiFi.reconnect();
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(internalLed, OUTPUT);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  if(!LittleFS.begin(true)) {
    Serial.println("LittleFS Mount Failed");
  }


  // --- Apply Static IP Configuration ---
  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS)) {
    Serial.println("STA Failed to configure Static IP");
  }

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.onEvent(WiFiEvent);

  WiFi.begin(ssid, password);

  Serial.print("Connecting");
  unsigned long startTime = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 20000) {
    Serial.print(".");
    delay(500);
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nRetrying...");
    WiFi.disconnect(true);
    delay(1000);
    WiFi.begin(ssid, password);
  }

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nConnected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  // --- SERVER ROUTES ---
  server.on("/", []() {
    if (!server.authenticate(www_username, www_password))
      return server.requestAuthentication();
    server.send(200, "text/html", getHTML());
  });

  server.on("/data", handleData);

  server.on("/start", []() {
    monitoringActive = true;
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/stop", []() {
    monitoringActive = false;
    digitalWrite(internalLed, LOW);
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/download", []() {
    File file = LittleFS.open("/log.csv", FILE_READ);
    if (!file) {
      server.send(404, "text/plain", "No log file");
      return;
    }
    server.streamFile(file, "text/csv");
    file.close();
  });

  server.begin();
}

void loop() {
  server.handleClient();

  if (monitoringActive) {
    digitalWrite(trigPin, LOW); delayMicroseconds(2);
    digitalWrite(trigPin, HIGH); delayMicroseconds(10);
    digitalWrite(trigPin, LOW);

    // Timeout added to prevent code hanging if sensor is unplugged
    long duration = pulseIn(echoPin, HIGH, 30000); 
    distance = duration * 0.0343 / 2;

    bool occupied = (distance > 0 && distance <= threshold);

    if (occupied != lastOccupiedState) {
      digitalWrite(internalLed, occupied);
      logEntry(occupied ? "OCCUPIED" : "VACANT");
      lastOccupiedState = occupied;
      Serial.println(occupied ? "OCCUPIED" : "VACANT");
    }

    delay(60); // Slightly increased delay for stability
  }
}
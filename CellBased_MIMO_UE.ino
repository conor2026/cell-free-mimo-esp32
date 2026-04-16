/*
 * ============================================================
 *  CELL-BASED MASSIVE MIMO — USER EQUIPMENT
 *  ESP32-S3-WROOM-1
 * ============================================================
 *  Upload this same sketch to all 3 UE boards.
 *  Change CLIENT_ID before uploading each one:
 *    Board 1: CLIENT_ID "UE1"
 *    Board 2: CLIENT_ID "UE2"
 *    Board 3: CLIENT_ID "UE3"
 * ============================================================
 */

#include <WiFi.h>
#include "esp_wifi.h"

// ── CHANGE THIS before uploading to each board ────────────
#define CLIENT_ID  "UE1"   // Change to UE2, UE3
// ──────────────────────────────────────────────────────────

const char* WIFI_SSID     = "CellBased_MIMO";
const char* WIFI_PASSWORD = "mimo1234";
const char* BS_IP         = "192.168.4.1";
const int   BS_PORT       = 8080;

#define TX_DELAY_MS   50     // ms between transmissions
#define TX_POWER      20     // TX power — lower for shorter range demo (try 8)
#define LED_PIN       48

const char* PAYLOAD = "Hello from " CLIENT_ID " — Cell-Based MIMO";

// ── Stats ─────────────────────────────────────────────────
int           seqNum         = 0;
int           txOK           = 0;
int           txFail         = 0;
int           failStreak     = 0;
unsigned long bytesInWindow  = 0;
unsigned long filesInWindow  = 0;
unsigned long lastReportTime = 0;

void blinkLED(int times, int ms = 50) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, HIGH); delay(ms);
    digitalWrite(LED_PIN, LOW);  delay(ms);
  }
}

bool ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  Serial.println("[" CLIENT_ID "] Reconnecting to BS...");
  WiFi.disconnect(true);
  delay(500);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 10000) {
    delay(300); Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    esp_wifi_set_max_tx_power(TX_POWER);
    int rssi = WiFi.RSSI();
    Serial.printf("[" CLIENT_ID "] Reconnected! IP: %s  RSSI: %d dBm\n",
                  WiFi.localIP().toString().c_str(), rssi);
    // Notify BS of reconnection
    WiFiClient tcp;
    if (tcp.connect(BS_IP, BS_PORT)) {
      tcp.println("CONNECT " CLIENT_ID " " + String(rssi));
      unsigned long t2 = millis();
      while (!tcp.available() && millis() - t2 < 2000) delay(10);
      tcp.stop();
    }
    return true;
  }
  Serial.println("[" CLIENT_ID "] Reconnect FAILED");
  return false;
}

String readLine(WiFiClient& client, int timeoutMs = 3000) {
  String line = "";
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    if (client.available()) {
      char c = client.read();
      if (c == '\n') return line;
      if (c != '\r') line += c;
      start = millis();
    }
    delay(1);
  }
  return line;
}

bool sendFile() {
  if (!ensureWiFi()) return false;

  int rssi        = WiFi.RSSI();
  int payloadSize = strlen(PAYLOAD);
  seqNum++;

  WiFiClient tcp;
  if (!tcp.connect(BS_IP, BS_PORT)) return false;

  // Include RSSI in command so BS can track signal strength
  tcp.println("SEND " CLIENT_ID " " + String(rssi) +
              " " + String(seqNum) +
              " " + String(payloadSize));

  String resp = readLine(tcp);
  if (resp != "READY") { tcp.stop(); return false; }

  tcp.print(PAYLOAD);
  String result = readLine(tcp);
  tcp.stop();

  if (result == "OK") {
    bytesInWindow += payloadSize;
    filesInWindow++;
    return true;
  }
  return false;
}

void printReport() {
  unsigned long now     = millis();
  unsigned long elapsed = now - lastReportTime;
  if (elapsed < 5000) return;

  float fps    = (float)filesInWindow / (elapsed / 1000.0f);
  float kbps   = (float)bytesInWindow  / (elapsed / 1000.0f) / 1024.0f;
  float lossPC = (txOK + txFail) > 0 ?
                 (float)txFail / (txOK + txFail) * 100.0f : 0.0f;
  int   rssi   = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;

  Serial.println("\n╔══════════════════════════════════════╗");
  Serial.printf( "║  CELL-BASED MIMO — %-17s║\n", CLIENT_ID);
  Serial.println("╠══════════════════════════════════════╣");
  Serial.printf( "║  BS SSID      : %-22s║\n", WIFI_SSID);
  Serial.printf( "║  RSSI to BS   : %4d dBm              ║\n", rssi);
  Serial.printf( "║  Files/sec    : %6.2f               ║\n", fps);
  Serial.printf( "║  Throughput   : %6.2f KB/s           ║\n", kbps);
  Serial.printf( "║  TX OK        : %6d               ║\n", txOK);
  Serial.printf( "║  TX Fail      : %6d  (%.1f%%)       ║\n", txFail, lossPC);
  Serial.printf( "║  Seq Num      : %6d               ║\n", seqNum);
  Serial.println("╚══════════════════════════════════════╝");

  filesInWindow  = 0;
  bytesInWindow  = 0;
  lastReportTime = now;
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  pinMode(LED_PIN, OUTPUT);
  blinkLED(3, 200);

  Serial.println("\n╔══════════════════════════════════════╗");
  Serial.printf( "║  CELL-BASED MIMO — %-17s║\n", CLIENT_ID);
  Serial.println("╠══════════════════════════════════════╣");
  Serial.printf( "║  Connecting to BS: %-18s║\n", WIFI_SSID);
  Serial.println("╚══════════════════════════════════════╝\n");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[" CLIENT_ID "] Connecting");

  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) {
    delay(400); Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    esp_wifi_set_max_tx_power(TX_POWER);
    int rssi = WiFi.RSSI();
    Serial.printf("[" CLIENT_ID "] Connected! IP: %s  RSSI: %d dBm\n",
                  WiFi.localIP().toString().c_str(), rssi);

    // Notify BS of initial connection
    WiFiClient tcp;
    if (tcp.connect(BS_IP, BS_PORT)) {
      tcp.println("CONNECT " CLIENT_ID " " + String(rssi));
      unsigned long t2 = millis();
      while (!tcp.available() && millis() - t2 < 2000) delay(10);
      tcp.stop();
    }
    blinkLED(5, 80);
  } else {
    Serial.println("[" CLIENT_ID "] WARNING: Not connected — will retry");
  }

  lastReportTime = millis();
  Serial.println("[" CLIENT_ID "] Starting continuous uplink transmission...\n");
}

void loop() {
  bool ok = sendFile();
  if (ok) {
    txOK++;
    failStreak = 0;
    digitalWrite(LED_PIN, HIGH); delay(15); digitalWrite(LED_PIN, LOW);
  } else {
    txFail++;
    failStreak++;
    seqNum--;
    if (failStreak >= 5) {
      Serial.printf("[" CLIENT_ID "] %d consecutive failures — reconnecting\n", failStreak);
      WiFi.disconnect(true);
      delay(500);
      failStreak = 0;
    }
  }

  printReport();
  delay(TX_DELAY_MS);
}

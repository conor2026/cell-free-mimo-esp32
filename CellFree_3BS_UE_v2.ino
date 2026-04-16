/*
 * ============================================================
 *  CELL-FREE MIMO — USER EQUIPMENT (UE) v2
 *  ESP32-S3-WROOM-1
 * ============================================================
 *  - Scans all 3 BS SSIDs using Wi-Fi scan (no connection needed)
 *  - Connects to the BS with the STRONGEST RSSI signal
 *  - Continuously sends files to connected BS
 *  - Re-scans every 5 seconds — hands over if a stronger BS found
 *  - Prints which BS it is connected to and current RSSI
 *  - Simulates mobile phone pinging off nearest cell tower
 * ============================================================
 */

#include <WiFi.h>
#include "esp_wifi.h"

// ── Base Station definitions ──────────────────────────────
struct BaseStation {
  const char* id;
  const char* ssid;
  const char* password;
  const char* ip;
  int         port;
};

BaseStation basestations[] = {
  { "BS1", "CellFree_BS1", "mimo1234", "192.168.4.1", 8080 },
  { "BS2", "CellFree_BS2", "mimo1234", "192.168.4.1", 8080 },
  { "BS3", "CellFree_BS3", "mimo1234", "192.168.4.1", 8080 },
};
const int NUM_BS = 3;

// ── Settings ──────────────────────────────────────────────
#define TX_DELAY_MS        50    // ms between file sends
#define HANDOVER_MARGIN    5     // dBm — only handover if new BS is this much stronger
#define SCAN_INTERVAL_MS   5000  // how often to scan for better BS
#define TX_POWER           20    // TX power (lower = shorter range, try 8 for short range demo)

#define LED_PIN 48

// ── State ─────────────────────────────────────────────────
int  currentBS      = -1;
int  seqNum         = 0;
int  txOK           = 0;
int  txFail         = 0;
int  handoverCount  = 0;
int  failStreak     = 0;
unsigned long bytesInWindow  = 0;
unsigned long filesInWindow  = 0;
unsigned long lastReportTime = 0;
unsigned long lastScanTime   = 0;

void blinkLED(int times, int ms = 50) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, HIGH); delay(ms);
    digitalWrite(LED_PIN, LOW);  delay(ms);
  }
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

// ── Scan all SSIDs and return index of strongest BS ───────
// Uses passive Wi-Fi scan — no connection needed
int findBestBS() {
  Serial.println("[UE] Scanning for base stations...");

  int bestIndex = -1;
  int bestRSSI  = -999;

  int n = WiFi.scanNetworks();
  if (n == 0) {
    Serial.println("[UE] No networks found");
    return -1;
  }

  Serial.printf("[UE] Found %d networks:\n", n);
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    int    rssi = WiFi.RSSI(i);
    Serial.printf("[UE]   %s  RSSI: %d dBm\n", ssid.c_str(), rssi);

    // Check if this network matches any of our BSs
    for (int b = 0; b < NUM_BS; b++) {
      if (ssid == basestations[b].ssid) {
        if (rssi > bestRSSI) {
          bestRSSI  = rssi;
          bestIndex = b;
        }
      }
    }
  }
  WiFi.scanDelete();

  if (bestIndex >= 0) {
    Serial.printf("[UE] Best BS: %s (RSSI: %d dBm)\n",
                  basestations[bestIndex].id, bestRSSI);
  } else {
    Serial.println("[UE] No known BS found in scan");
  }

  return bestIndex;
}

// ── Connect to a specific BS ──────────────────────────────
bool connectToBS(int bsIndex) {
  BaseStation& bs = basestations[bsIndex];
  Serial.printf("[UE] Connecting to %s (%s)...\n", bs.id, bs.ssid);

  WiFi.disconnect(true);
  delay(300);
  WiFi.begin(bs.ssid, bs.password);

  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 8000) {
    delay(200); Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("[UE] Failed to connect to %s\n", bs.id);
    return false;
  }

  // Set TX power for demo range effect
  esp_wifi_set_max_tx_power(TX_POWER);

  // Notify BS
  WiFiClient tcp;
  if (tcp.connect(bs.ip, bs.port)) {
    tcp.println("CONNECT");
    readLine(tcp, 1000);
    tcp.stop();
  }

  Serial.printf("[UE] ★ Connected to %s  IP: %s  RSSI: %d dBm\n",
                bs.id, WiFi.localIP().toString().c_str(), WiFi.RSSI());
  return true;
}

// ── Scan and handover if better BS found ─────────────────
void scanAndHandover(bool force = false) {
  Serial.println("\n[UE] ━━━ Scanning for best base station ━━━");

  int bestIndex = findBestBS();
  if (bestIndex < 0) {
    Serial.println("[UE] No BS found — retrying...");
    return;
  }

  int currentRSSI = (currentBS >= 0 && WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -999;

  // Only handover if new BS is significantly stronger or we have no connection
  bool shouldHandover = force ||
                        currentBS < 0 ||
                        bestIndex != currentBS ||
                        WiFi.status() != WL_CONNECTED;

  // Check if new BS is stronger by HANDOVER_MARGIN
  if (!shouldHandover && bestIndex != currentBS) {
    int scannedRSSI = -999;
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n; i++) {
      if (String(WiFi.SSID(i)) == basestations[bestIndex].ssid) {
        scannedRSSI = WiFi.RSSI(i);
        break;
      }
    }
    WiFi.scanDelete();
    if (scannedRSSI > currentRSSI + HANDOVER_MARGIN) shouldHandover = true;
  }

  if (!shouldHandover) {
    Serial.printf("[UE] Staying on %s (RSSI: %d dBm)\n",
                  basestations[currentBS].id, currentRSSI);
    return;
  }

  int prevBS = currentBS;
  if (connectToBS(bestIndex)) {
    currentBS = bestIndex;
    if (prevBS != currentBS) {
      handoverCount++;
      Serial.println("\n╔══════════════════════════════════════╗");
      Serial.printf( "║  ★ HANDOVER #%-3d                     ║\n", handoverCount);
      Serial.printf( "║  FROM : %-28s║\n", prevBS >= 0 ? basestations[prevBS].id : "NONE");
      Serial.printf( "║  TO   : %-28s║\n", basestations[currentBS].id);
      Serial.printf( "║  RSSI : %4d dBm                     ║\n", WiFi.RSSI());
      Serial.println("╚══════════════════════════════════════╝\n");
      blinkLED(5, 60);
      failStreak = 0;
    }
  }
}

// ── Send one file to current BS ───────────────────────────
bool sendFile() {
  if (currentBS < 0 || WiFi.status() != WL_CONNECTED) return false;

  BaseStation& bs = basestations[currentBS];
  seqNum++;

  String payload  = "UE→" + String(bs.id) + " seq=" + String(seqNum);
  int payloadSize = payload.length();

  WiFiClient tcp;
  if (!tcp.connect(bs.ip, bs.port)) return false;

  tcp.println("SEND " + String(seqNum) + " " + String(payloadSize));
  String resp = readLine(tcp);
  if (resp != "READY") { tcp.stop(); return false; }

  tcp.print(payload);
  String result = readLine(tcp);
  tcp.stop();

  if (result == "OK") {
    bytesInWindow  += payloadSize;
    filesInWindow++;
    return true;
  }
  return false;
}

// ── Print UE stats every 5 seconds ───────────────────────
void printReport() {
  unsigned long now     = millis();
  unsigned long elapsed = now - lastReportTime;
  if (elapsed < 5000) return;

  float fps    = (float)filesInWindow / (elapsed / 1000.0f);
  float kbps   = (float)bytesInWindow  / (elapsed / 1000.0f) / 1024.0f;
  float lossPC = (txOK + txFail) > 0 ?
                 (float)txFail / (txOK + txFail) * 100.0f : 0.0f;
  int rssi = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;

  Serial.println("\n╔══════════════════════════════════════╗");
  Serial.println("║       CELL-FREE MIMO — UE            ║");
  Serial.println("╠══════════════════════════════════════╣");
  Serial.printf( "║  Connected BS : %-22s║\n",
                 currentBS >= 0 ? basestations[currentBS].id : "NONE");
  Serial.printf( "║  RSSI         : %4d dBm              ║\n", rssi);
  Serial.printf( "║  Files/sec    : %6.2f               ║\n", fps);
  Serial.printf( "║  Throughput   : %6.2f KB/s           ║\n", kbps);
  Serial.printf( "║  TX OK        : %6d               ║\n", txOK);
  Serial.printf( "║  TX Fail      : %6d  (%.1f%%)       ║\n", txFail, lossPC);
  Serial.printf( "║  Handovers    : %6d               ║\n", handoverCount);
  Serial.printf( "║  Seq Num      : %6d               ║\n", seqNum);
  Serial.println("╚══════════════════════════════════════╝");

  filesInWindow  = 0;
  bytesInWindow  = 0;
  lastReportTime = now;
}

// ─────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(2000);
  pinMode(LED_PIN, OUTPUT);
  blinkLED(3, 200);

  Serial.println("\n╔══════════════════════════════════════╗");
  Serial.println("║    CELL-FREE MIMO UE STARTING        ║");
  Serial.println("╚══════════════════════════════════════╝\n");

  WiFi.mode(WIFI_STA);

  // Keep scanning until we find a BS
  while (currentBS < 0) {
    scanAndHandover(true);
    if (currentBS < 0) delay(2000);
  }

  lastReportTime = millis();
  lastScanTime   = millis();
  Serial.println("[UE] Starting continuous uplink transmission...\n");
}

void loop() {
  // Periodic rescan for better BS
  if (millis() - lastScanTime > SCAN_INTERVAL_MS) {
    scanAndHandover(false);
    lastScanTime = millis();
  }

  // Send file
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
      Serial.printf("[UE] %d consecutive failures — forcing rescan\n", failStreak);
      scanAndHandover(true);
      failStreak = 0;
    }
  }

  printReport();
  delay(TX_DELAY_MS);
}

/*
 * ============================================================
 *  CELL-BASED MASSIVE MIMO — BASE STATION (1 BS + 3 UEs)
 *  ESP32-S3-WROOM-1
 * ============================================================
 *  - Creates Wi-Fi Access Point
 *  - Receives files from up to 3 UEs simultaneously
 *  - Tracks per-UE RSSI, files/sec, throughput
 *  - Prints full report every 5 seconds
 * ============================================================
 */

#include <WiFi.h>

const char* AP_SSID     = "CellBased_MIMO";
const char* AP_PASSWORD = "mimo1234";
#define PORT    8080
#define LED_PIN 48
#define MAX_UE  3

WiFiServer tcpServer(PORT);

// ── Per-UE stats ──────────────────────────────────────────
struct UEStat {
  String id;
  int    rssi;
  int    totalFiles;
  int    filesInWindow;
  unsigned long bytesInWindow;
  unsigned long totalBytes;
  unsigned long lastSeen;
  bool   connected;
};

UEStat ueStats[MAX_UE];
int    ueCount = 0;

// ── Global stats ──────────────────────────────────────────
int           totalFiles     = 0;
unsigned long totalBytes     = 0;
unsigned long lastReportTime = 0;
unsigned long systemStart    = 0;

void blinkLED(int times, int ms = 80) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, HIGH); delay(ms);
    digitalWrite(LED_PIN, LOW);  delay(ms);
  }
}

// Find or create UE stat entry
UEStat* getUE(const String& id) {
  for (int i = 0; i < ueCount; i++) {
    if (ueStats[i].id == id) return &ueStats[i];
  }
  if (ueCount < MAX_UE) {
    ueStats[ueCount] = { id, 0, 0, 0, 0, 0, millis(), false };
    return &ueStats[ueCount++];
  }
  return nullptr;
}

void printReport() {
  unsigned long now     = millis();
  unsigned long elapsed = now - lastReportTime;
  if (elapsed < 5000) return;

  // Check for disconnected UEs (no file in 4 seconds)
  for (int i = 0; i < ueCount; i++) {
    if (ueStats[i].connected && (now - ueStats[i].lastSeen) > 4000) {
      ueStats[i].connected = false;
      Serial.printf("[BS] *** %s DISCONNECTED ***\n", ueStats[i].id.c_str());
    }
  }

  // Global rates
  unsigned long totalWindowBytes = 0;
  int           totalWindowFiles = 0;
  for (int i = 0; i < ueCount; i++) {
    totalWindowBytes += ueStats[i].bytesInWindow;
    totalWindowFiles += ueStats[i].filesInWindow;
  }

  float totalFps  = (float)totalWindowFiles / (elapsed / 1000.0f);
  float totalKbps = (float)totalWindowBytes  / (elapsed / 1000.0f) / 1024.0f;
  float uptime    = (now - systemStart) / 1000.0f;

  Serial.println("\n╔══════════════════════════════════════════╗");
  Serial.println("║   CELL-BASED MIMO — BASE STATION         ║");
  Serial.println("╠══════════════════════════════════════════╣");
  Serial.printf( "║  Uptime       : %7.1f sec               ║\n", uptime);
  Serial.printf( "║  Total files  : %7d                  ║\n", totalFiles);
  Serial.printf( "║  Files/sec    : %7.2f                  ║\n", totalFps);
  Serial.printf( "║  Throughput   : %7.2f KB/s              ║\n", totalKbps);
  Serial.println("╠══════════════════════════════════════════╣");
  Serial.println("║  PER-UE BREAKDOWN                        ║");
  Serial.println("╠════════════╦═══════╦══════════╦══════════╣");
  Serial.println("║  UE ID     ║ RSSI  ║ Files/s  ║ Status   ║");
  Serial.println("╠════════════╬═══════╬══════════╬══════════╣");

  for (int i = 0; i < ueCount; i++) {
    float fps = (float)ueStats[i].filesInWindow / (elapsed / 1000.0f);
    Serial.printf("║  %-10s║ %4d  ║ %6.2f   ║ %-8s ║\n",
                  ueStats[i].id.c_str(),
                  ueStats[i].rssi,
                  fps,
                  ueStats[i].connected ? "ONLINE" : "OFFLINE");
  }

  // Fill empty UE slots
  for (int i = ueCount; i < MAX_UE; i++) {
    Serial.println("║  --        ║  ---  ║  ---    ║ WAITING  ║");
  }

  Serial.println("╚════════════╩═══════╩══════════╩══════════╝");

  // Reset window counters
  for (int i = 0; i < ueCount; i++) {
    ueStats[i].filesInWindow = 0;
    ueStats[i].bytesInWindow = 0;
  }
  lastReportTime = now;
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

void handleClient(WiFiClient& client) {
  String cmd = readLine(client);
  cmd.trim();

  // CONNECT <clientID> <rssi>
  if (cmd.startsWith("CONNECT ")) {
    int sp      = cmd.indexOf(' ', 8);
    String id   = cmd.substring(8, sp);
    int rssi    = cmd.substring(sp + 1).toInt();
    UEStat* ue  = getUE(id);
    if (ue) {
      bool wasConnected = ue->connected;
      ue->rssi      = rssi;
      ue->connected = true;
      ue->lastSeen  = millis();
      if (!wasConnected) {
        Serial.printf("\n[BS] *** %s CONNECTED  RSSI: %d dBm ***\n\n",
                      id.c_str(), rssi);
        blinkLED(2, 80);
      }
    }
    client.println("OK");
    return;
  }

  // SEND <clientID> <rssi> <seqNum> <size>
  if (cmd.startsWith("SEND ")) {
    // Parse: SEND UE1 -45 123 25
    int s1 = cmd.indexOf(' ', 5);
    int s2 = cmd.indexOf(' ', s1 + 1);
    int s3 = cmd.indexOf(' ', s2 + 1);

    if (s1 < 0 || s2 < 0 || s3 < 0) {
      client.println("ERROR Bad format");
      return;
    }

    String id    = cmd.substring(5, s1);
    int    rssi  = cmd.substring(s1 + 1, s2).toInt();
    int    seq   = cmd.substring(s2 + 1, s3).toInt();
    int    size  = cmd.substring(s3 + 1).toInt();

    client.println("READY");

    // Receive payload
    uint8_t buf[256];
    int remaining = size;
    unsigned long lastData = millis();
    while (remaining > 0 && (millis() - lastData) < 5000) {
      if (client.available()) {
        int got = client.read(buf, min(remaining, (int)sizeof(buf)));
        if (got > 0) { remaining -= got; lastData = millis(); }
      }
      delay(1);
    }

    if (remaining == 0) {
      UEStat* ue = getUE(id);
      if (ue) {
        ue->rssi           = rssi;
        ue->totalFiles++;
        ue->filesInWindow++;
        ue->totalBytes    += size;
        ue->bytesInWindow += size;
        ue->lastSeen       = millis();
        ue->connected      = true;
      }
      totalFiles++;
      totalBytes += size;
      client.println("OK");
      Serial.printf("[BS] RX %s seq=%d RSSI=%ddBm size=%dB total=%d\n",
                    id.c_str(), seq, rssi, size, totalFiles);
      digitalWrite(LED_PIN, HIGH); delay(10); digitalWrite(LED_PIN, LOW);
    } else {
      client.println("ERROR");
    }
    return;
  }

  client.println("ERROR Unknown");
}

void setup() {
  Serial.begin(115200);
  delay(500);
  pinMode(LED_PIN, OUTPUT);
  blinkLED(3, 200);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  delay(500);

  systemStart    = millis();
  lastReportTime = millis();

  Serial.println("\n╔══════════════════════════════════════════╗");
  Serial.println("║   CELL-BASED MASSIVE MIMO — BS READY     ║");
  Serial.println("╠══════════════════════════════════════════╣");
  Serial.printf( "║  SSID : %-32s║\n", AP_SSID);
  Serial.printf( "║  IP   : %-32s║\n", WiFi.softAPIP().toString().c_str());
  Serial.printf( "║  Port : %-32d║\n", PORT);
  Serial.println("╚══════════════════════════════════════════╝\n");

  tcpServer.begin();
  blinkLED(5, 80);
}

void loop() {
  WiFiClient client = tcpServer.available();
  if (client) {
    handleClient(client);
    client.stop();
  }
  printReport();
}

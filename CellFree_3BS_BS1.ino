/*
 * ============================================================
 *  CELL-FREE MIMO — BASE STATION 1
 *  ESP32-S3-WROOM-1
 * ============================================================
 *  Upload this to Device 1 (BS1)
 *  Change SSID/IP below for BS2 and BS3
 * ============================================================
 */

#include <WiFi.h>

// ── CHANGE for each BS ────────────────────────────────────
#define BS_ID       "BS1"
#define AP_SSID     "CellFree_BS1"
#define AP_PASSWORD "mimo1234"
// ──────────────────────────────────────────────────────────

#define PORT    8080
#define LED_PIN 48

WiFiServer tcpServer(PORT);

// ── Stats ─────────────────────────────────────────────────
int           totalFiles       = 0;
int           filesInWindow    = 0;
unsigned long bytesInWindow    = 0;
unsigned long totalBytes       = 0;
unsigned long lastReportTime   = 0;
unsigned long lastFileTime     = 0;
bool          ueConnected      = false;

void blinkLED(int times, int ms = 80) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, HIGH); delay(ms);
    digitalWrite(LED_PIN, LOW);  delay(ms);
  }
}

void printReport() {
  unsigned long now     = millis();
  unsigned long elapsed = now - lastReportTime;
  if (elapsed < 5000) return;

  float fps  = (float)filesInWindow / (elapsed / 1000.0f);
  float kbps = (float)bytesInWindow  / (elapsed / 1000.0f) / 1024.0f;

  Serial.println("\n╔══════════════════════════════════════╗");
  Serial.printf( "║  CELL-FREE MIMO — %-18s║\n", BS_ID);
  Serial.println("╠══════════════════════════════════════╣");
  Serial.printf( "║  UE Connected : %-22s║\n", ueConnected ? "YES" : "NO");
  Serial.printf( "║  Files/sec    : %6.2f               ║\n", fps);
  Serial.printf( "║  Throughput   : %6.2f KB/s           ║\n", kbps);
  Serial.printf( "║  Total files  : %6d               ║\n", totalFiles);
  Serial.printf( "║  Total data   : %6.2f KB             ║\n", totalBytes / 1024.0f);
  Serial.println("╚══════════════════════════════════════╝");

  filesInWindow = 0;
  bytesInWindow = 0;
  lastReportTime = now;

  // Mark UE as disconnected if no file received in 3 seconds
  if (ueConnected && (millis() - lastFileTime) > 3000) {
    ueConnected = false;
    Serial.printf("\n[%s] *** UE DISCONNECTED — handover likely ***\n\n", BS_ID);
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

void handleClient(WiFiClient& client) {
  String cmd = readLine(client);
  cmd.trim();

  if (cmd == "PING") {
    client.println("PONG:" BS_ID);
    Serial.printf("[%s] PING received — responded PONG\n", BS_ID);
    return;
  }

  if (cmd.startsWith("CONNECT")) {
    if (!ueConnected) {
      Serial.printf("\n[%s] *** UE CONNECTED — handover to %s ***\n\n", BS_ID, BS_ID);
      ueConnected = true;
      blinkLED(3, 100);
    }
    client.println("OK:" BS_ID);
    return;
  }

  if (cmd.startsWith("SEND ")) {
    int sp       = cmd.lastIndexOf(' ');
    int fileSize = cmd.substring(sp + 1).toInt();
    int seqNum   = cmd.substring(5, sp).toInt();

    client.println("READY");

    uint8_t buf[256];
    int remaining = fileSize;
    unsigned long lastData = millis();

    while (remaining > 0 && (millis() - lastData) < 5000) {
      if (client.available()) {
        int toRead = min(remaining, (int)sizeof(buf));
        int got    = client.read(buf, toRead);
        if (got > 0) { remaining -= got; lastData = millis(); }
      }
      delay(1);
    }

    if (remaining == 0) {
      totalFiles++;
      filesInWindow++;
      totalBytes    += fileSize;
      bytesInWindow += fileSize;
      lastFileTime   = millis();
      if (!ueConnected) {
        ueConnected = true;
        Serial.printf("\n[%s] *** UE CONNECTED ***\n\n", BS_ID);
        blinkLED(3, 100);
      }
      client.println("OK");
      Serial.printf("[%s] RX seq=%d (%d B) total=%d\n",
                    BS_ID, seqNum, fileSize, totalFiles);
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

  lastReportTime = millis();
  lastFileTime   = millis();

  Serial.println("\n╔══════════════════════════════════════╗");
  Serial.printf( "║  CELL-FREE MIMO — %-18s║\n", BS_ID);
  Serial.println("╠══════════════════════════════════════╣");
  Serial.printf( "║  SSID : %-28s║\n", AP_SSID);
  Serial.printf( "║  IP   : %-28s║\n", WiFi.softAPIP().toString().c_str());
  Serial.printf( "║  Port : %-28d║\n", PORT);
  Serial.println("╚══════════════════════════════════════╝\n");

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

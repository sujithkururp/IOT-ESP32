#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "SPIFFS.h"

#include <IRrecv.h>
#include <IRsend.h>
#include <IRremoteESP8266.h>

// ================= WIFI =================
const char* ssid = "AK";
const char* password = "12345670";

// ================= IR =================
#define RECV_PIN 14
#define IR_LED   4

#define CAPTURE_BUFFER_SIZE 1024
#define TIMEOUT 50
#define MAX_RAW 300

IRrecv irrecv(RECV_PIN, CAPTURE_BUFFER_SIZE, TIMEOUT, true);
IRsend irsend(IR_LED);
decode_results results;

// ================= SERVER =================
WebServer server(80);

// ================= STORAGE =================
uint16_t refData[MAX_RAW], tempData[MAX_RAW], finalData[MAX_RAW];
uint16_t refLen = 0, tempLen = 0, finalLen = 0;

int matchCount = 0;
bool learningActive = false;
bool learnedReady = false;

// ================= FILESYSTEM =================
void initFS() {
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount Failed");
  }
}

// ================= MATCH =================
bool isSimilar(uint16_t *a, uint16_t *b, int lenA, int lenB) {

  if (abs(lenA - lenB) > 10) return false;

  int minLen = min(lenA, lenB);
  int mismatch = 0;

  for (int i = 4; i < minLen - 4; i++) {
    int maxVal = max(a[i], b[i]);
    int diff = abs((int)a[i] - (int)b[i]);

    int tolerance = maxVal * 0.75 + 300;

    if (diff > tolerance) mismatch++;
  }

  return mismatch < (minLen * 0.30);
}

// ================= CAPTURE =================
bool capture(uint16_t *data, uint16_t &len) {

  if (irrecv.decode(&results)) {

    len = results.rawlen - 1;
    if (len > MAX_RAW) len = MAX_RAW;

    for (int i = 1; i < results.rawlen && i <= MAX_RAW; i++) {
      data[i - 1] = results.rawbuf[i] * kRawTick;
    }

    Serial.println("Captured Len: " + String(len));

    irrecv.resume();
    return true;
  }
  return false;
}

// ================= LEARNING =================
void learningLoop() {

  if (!learningActive) return;

  if (capture(tempData, tempLen)) {

    Serial.println("Signal received");

    if (matchCount == 0) {
      memcpy(refData, tempData, sizeof(uint16_t) * tempLen);
      refLen = tempLen;
      matchCount = 1;
      Serial.println("Reference stored");
    }
    else {
      if (isSimilar(refData, tempData, refLen, tempLen)) {

        matchCount++;
        Serial.println("Match: " + String(matchCount));

        if (matchCount >= 4) {
          memcpy(finalData, refData, sizeof(uint16_t) * refLen);
          finalLen = refLen;

          learnedReady = true;
          learningActive = false;

          Serial.println("✅ LEARNING COMPLETE");
        }

      } else {
        Serial.println("❌ Mismatch → resetting");

        memcpy(refData, tempData, sizeof(uint16_t) * tempLen);
        refLen = tempLen;
        matchCount = 1;
      }
    }
  }
}

// ================= SAVE =================
void saveCommand(String name) {

  File file = SPIFFS.open("/" + name + ".txt", FILE_WRITE);

  file.println(finalLen);

  for (int i = 0; i < finalLen; i++) {
    file.print(finalData[i]);
    if (i < finalLen - 1) file.print(",");
  }

  file.close();

  Serial.println("Saved: " + name);
}

// ================= DELETE =================
void deleteCommand(String name) {
  SPIFFS.remove("/" + name + ".txt");
  Serial.println("Deleted: " + name);
}

// ================= LOAD =================
bool loadCommand(String name, uint16_t *data, uint16_t &len) {

  File file = SPIFFS.open("/" + name + ".txt");
  if (!file) return false;

  len = file.readStringUntil('\n').toInt();
  String line = file.readStringUntil('\n');
  file.close();

  int index = 0;
  char *token = strtok((char*)line.c_str(), ",");

  while (token && index < len) {
    data[index++] = atoi(token);
    token = strtok(NULL, ",");
  }

  return true;
}

// ================= MAIN PAGE =================
String mainPage() {

  String html = "<h2>AC Remote</h2>";
  html += "<button onclick=\"location.href='/config'\">⚙️ Configure</button><br><br>";

  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  while (file) {
    String name = String(file.name());
    name.replace("/", "");
    name.replace(".txt", "");

    html += "<button onclick=\"sendCmd('" + name + "')\">" + name + "</button>";
    html += "<button onclick=\"delCmd('" + name + "')\">❌</button><br>";

    file = root.openNextFile();
  }

  html += R"rawliteral(
<script>
function sendCmd(n){
 fetch('/send?name='+n);
}

function delCmd(n){
 if(confirm("Delete "+n+"?")){
   fetch('/delete?name='+n).then(()=>location.reload());
 }
}
</script>
)rawliteral";

  return html;
}

// ================= CONFIG PAGE =================
String configPage = R"rawliteral(
<h2>Configuration</h2>

<input id="name" placeholder="Button name"><br><br>

<button onclick="start()">Start Learning</button><br><br>

<p id="status">Idle</p>

<button id="saveBtn" onclick="save()" disabled>Save</button>
<button onclick="discard()">Discard</button>

<script>
let interval;

function start(){
 fetch('/start');
 document.getElementById("status").innerText = "Learning...";
 interval = setInterval(check, 1000);
}

function check(){
 fetch('/status')
 .then(r=>r.text())
 .then(t=>{
   document.getElementById("status").innerText = t;

   if(t.includes("READY")){
     document.getElementById("saveBtn").disabled = false;
     clearInterval(interval);
   }
 });
}

function save(){
 let name = document.getElementById("name").value;
 fetch('/save?name='+name).then(()=>location.href='/');
}

function discard(){
 fetch('/discard').then(()=>location.href='/');
}
</script>
)rawliteral";

// ================= ROUTES =================
void handleRoot() { server.send(200, "text/html", mainPage()); }
void handleConfig() { server.send(200, "text/html", configPage); }

void handleStart() {
  learningActive = true;
  learnedReady = false;
  matchCount = 0;
  server.send(200, "text/plain", "Started");
}

void handleStatus() {
  if (learnedReady)
    server.send(200, "text/plain", "READY");
  else
    server.send(200, "text/plain", "Matching: " + String(matchCount) + "/4");
}

void handleSave() {
  if (!server.hasArg("name") || !learnedReady) {
    server.send(200, "text/plain", "Error");
    return;
  }

  saveCommand(server.arg("name"));
  learnedReady = false;

  server.send(200, "text/plain", "Saved");
}

void handleDiscard() {
  learningActive = false;
  learnedReady = false;
  matchCount = 0;
  server.send(200, "text/plain", "Discarded");
}

void handleSend() {
  String name = server.arg("name");

  uint16_t data[MAX_RAW];
  uint16_t len;

  if (loadCommand(name, data, len)) {
    irsend.sendRaw(data, len, 38);
    server.send(200, "text/plain", "Sent");
  } else {
    server.send(200, "text/plain", "Load failed");
  }
}

void handleDelete() {
  if (!server.hasArg("name")) {
    server.send(200, "text/plain", "No name");
    return;
  }

  deleteCommand(server.arg("name"));
  server.send(200, "text/plain", "Deleted");
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);

  initFS();

  irrecv.enableIRIn();
  irsend.begin();

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) delay(500);

  Serial.println("\nConnected!");
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.on("/config", handleConfig);
  server.on("/start", handleStart);
  server.on("/status", handleStatus);
  server.on("/save", handleSave);
  server.on("/discard", handleDiscard);
  server.on("/send", handleSend);
  server.on("/delete", handleDelete);

  server.begin();
}

// ================= LOOP =================
void loop() {
  server.handleClient();
  learningLoop();
}

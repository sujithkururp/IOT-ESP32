#include <WiFi.h>


void setup() {

  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.begin("AK","12345678");
  Serial.print("connecting to wifi");
  while(WiFi.status() !=WL_CONNECTED);
  {
    Serial.print(".");
    delay(200);
  }

  Serial.print("IP address");
  Serial.println(WiFi.localIP());
  Serial.print("macAddress");
  Serial.println(WiFi.macAddress());
  // put your setup code here, to run once:

}

void loop() {
  // put your main code here, to run repeatedly:

}

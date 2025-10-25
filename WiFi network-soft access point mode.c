#include <WiFi.h>
#include<DNSServer.h>
#include<WiFiManager.h>


WiFiManager wifi;
void setup() {

  Serial.begin(115200);
  wifi.autoConnect("AK");
  Serial.println("connect to ak");
 
 

}

void loop() {
  // put your main code here, to run repeatedly:

}

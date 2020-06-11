/* Controlling LED using Firebase console originally made by CircuitDigest(www.circuitdigest.com) */
/* Modified by Marcelo Pimentel to use FirebaseESP8266 instead of FirebaseArduino */
/* original script can be found on https://circuitdigest.com/microcontroller-projects/iot-firebase-controlled-led-using-esp8266-nodemcu */

#include <ESP8266WiFi.h>
#include "FirebaseESP8266.h"

#define FIREBASE_HOST "https://FIREBASEHOST.com/"
#define FIREBASE_AUTH "FIREBASEAUTH"

// input your home or public wifi name 
#define WIFI_SSID "YOUR_WIFI_SSID"
//password of wifi ssid
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"                                    

FirebaseData firebaseData;
bool isLedOn = false;
int led = D3;

void setup() {
  Serial.begin(9600);
  delay(1000);
  pinMode(LED_BUILTIN, OUTPUT);      
  pinMode(led, OUTPUT);                 
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to ");
  Serial.print(WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();
  Serial.print("Connected to ");
  Serial.println(WIFI_SSID);
  Serial.print("IP Address is : ");
  Serial.println(WiFi.localIP());
  Firebase.begin(FIREBASE_HOST, FIREBASE_AUTH);
  Firebase.setBool(firebaseData, "/isLedOn", false);
}

void loop() {
  Firebase.getBool(firebaseData, "/isLedOn");
  isLedOn = firebaseData.boolData();
  if (isLedOn == true) {
    Serial.println("Led Turned ON");
    digitalWrite(LED_BUILTIN, LOW);
    digitalWrite(led, HIGH);
  } 
  else if (isLedOn == false) {
    Serial.println("Led Turned OFF");
    digitalWrite(LED_BUILTIN, HIGH);
    digitalWrite(led, LOW);
  }
  else {
    Serial.println("Wrong Credential! Please send ON/OFF");
  }
}

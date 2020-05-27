#include <OneWire.h>
#include <DallasTemperature.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include "WiFiManager.h"
#include <ESP8266HTTPClient.h>
#include <SoftwareSerial.h>
#include "FirebaseESP8266.h"
#include <time.h>

//Firebase connection params, you can set up Firebase according to this guide https://circuitdigest.com/microcontroller-projects/iot-firebase-controlled-led-using-esp8266-nodemcu
//Also, you need to create those 3 fields in the database: isControllingTemp, targetTemp and targetTempError (Firebase_realtime_database_setup.png)
#define FIREBASE_HOST "https://FIREBASEHOST.com/"
#define FIREBASE_AUTH "FIREBASEAUTH"

#define TEMP_SENSOR D4
#define RELAY_PUMP D5
#define RELAY_HEATER D6

#define MAX_FAIL_TEMPERATURE 10

FirebaseData firebaseData;

OneWire barramento(TEMP_SENSOR);
DallasTemperature sensor(&barramento);

bool isControllingTemp;
float targetTemp;
float targetTemp_error;

float currentTemperature;
float lastTemperature;
float startTemperature = -1;
bool heaterStatus = false;
bool pumpStatus = false;

bool hitTargetTemp = false;
bool wasControllingTemp = false;

//Get the current time with a user friendly timestamp (UTC)
String getCurrentTime(){
  String timestamp = "";

  time_t now = time(nullptr);
  struct tm * timeinfo;
  time(&now);
  timeinfo = localtime(&now);

  static char charTime[26];
  
  sprintf(charTime, "%04d-%02d-%02d %02d:%02d:%02d",
  1900 + timeinfo->tm_year,
  1 + timeinfo->tm_mon,
  timeinfo->tm_mday,
  timeinfo->tm_hour,
  timeinfo->tm_min,
  timeinfo->tm_sec);

  timestamp = String(charTime);
  
  return timestamp;
}

//This function is called when the device can not acquire reliable temperature read. The pump and heater are turned off and the bi-color led start blinking red.
void emergencyShutdown(){
  digitalWrite(RELAY_HEATER, LOW);
  digitalWrite(RELAY_PUMP, LOW);
  digitalWrite(D1, LOW);
  while(true){
    digitalWrite(D2, HIGH);
    delay(1000);
    digitalWrite(D2, LOW);
    delay(1000);
  }
}

//Gets first reading of current time
void setupTime(){
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    Serial.print("\nAguardando horário");
    while (!time(nullptr)) {
      Serial.print(".");
      delay(500);
    }
  
    while (getCurrentTime().substring(0,4) == "1970") {
      Serial.print(".");
      delay(500);
    }
  
    Serial.print("OK!");
    Serial.println(getCurrentTime());
}

//With callback setup
void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  Serial.println(myWiFiManager->getConfigPortalSSID());
}

//Update current temperature (after getting first reading)
void updateCurrentTemperature(){
  float diff = -100;
  int count = 0;
  while((diff >= 5) | (diff <= -5)){
    sensor.requestTemperatures();
    currentTemperature = sensor.getTempCByIndex(0);
    diff = currentTemperature - lastTemperature;
    if(!((diff >= 5) | (diff <= -5))){
      break;
    }
    delay(1000);
    count = count + 1;
    if(count > MAX_FAIL_TEMPERATURE){
      emergencyShutdown();
    }
  }
  lastTemperature = currentTemperature;
}

//Gets reliable temperature (check reading more than one time) and check if there is big difference between readings
void getReliableTemperature(){
  int loops = 3;
  int i = 1;
  float sumOfReadings = 0;
  float reading;

  Serial.println("Getting reliable temp...");
  
  while(i <= loops){
    sensor.requestTemperatures();
    reading = sensor.getTempCByIndex(0);
    sumOfReadings = sumOfReadings + reading;
    i = i + 1;
    Serial.println(reading);
    delay(1000);
  }
  float meanReading = sumOfReadings/loops;
  lastTemperature = meanReading;

  sensor.requestTemperatures();
  currentTemperature = sensor.getTempCByIndex(0);
  Serial.println(currentTemperature);
  float diff = currentTemperature - lastTemperature;
  if((diff >= 5) | (diff <= -5)){
    getReliableTemperature();
  }
  
}

//Get Firebase data
int getSettingsFromWeb(){
  if (Firebase.getFloat(firebaseData, "/targetTemp")) {
    if  ((firebaseData.dataType() == "float") | (firebaseData.dataType() == "int")) {
      targetTemp = firebaseData.floatData();
    }else{
      return 1;
    }
  }else{
    return 1;
  }
  if (Firebase.getFloat(firebaseData, "/targetTempError")) {
    if  (((firebaseData.dataType() == "float") | (firebaseData.dataType() == "int")) & firebaseData.floatData() > 0) {
      targetTemp_error = firebaseData.floatData();
    }else{
      return 1;
    }
  }else{
    return 1;
  }
  if (Firebase.getBool(firebaseData, "/isControllingTemp")) {
    if  (firebaseData.dataType() == "boolean") {
      isControllingTemp = firebaseData.boolData();
    }else{
      return 1;
    }
  }else{
    return 1;
  }
  return 0;
}

//Records times to Firebase when the device starts controlling the temperature
int recordStartTimeIntoWeb(){
  if (!Firebase.setString(firebaseData, "/startTime", getCurrentTime())) {
    return 1;
  }
  if (!Firebase.setString(firebaseData, "/stopTime", "")) {
    return 1;
  }
  if (!Firebase.setString(firebaseData, "/targetTemperatureHitTime", "")) {
    return 1;
  }
  return 0;
}

//Records to Firebase the time when the device stopped controlling the temperature
int recordStopTimeIntoWeb(){
  if (!Firebase.setString(firebaseData, "/stopTime", getCurrentTime())) {
    return 1;
  }
  return 0;
}

//Records to Firebase the time when the device first hit the target temperature
int recordTargetTemperatureHitIntoWeb(){
  if (!Firebase.setString(firebaseData, "/targetTemperatureHitTime", getCurrentTime())) {
    return 1;
  }
  return 0;
}

//Records to Firebase the current status
int recordDataIntoWeb(){
  String currDate = getCurrentTime();
  if (!Firebase.setFloat(firebaseData, "/currentTemp", currentTemperature)) {
    return 1;
  }
  if (!Firebase.setFloat(firebaseData, "/startTemp", startTemperature)) {
    return 1;
  }
  if (!Firebase.setBool(firebaseData, "/isHeaterOn", heaterStatus)) {
    return 1;
  }
  if (!Firebase.setBool(firebaseData, "/isPumpOn", pumpStatus)) {
    return 1;
  }
  if (!Firebase.setString(firebaseData, "/lastActivityTime", currDate)) {
    return 1;
  }
  return 0;
}

//Turn the bi-color LED to red
void double_led_red(){
  digitalWrite(D2, HIGH);
  digitalWrite(D1, LOW);
}

//Turn the bi-color LED to green
void double_led_green(){
  digitalWrite(D2, LOW);
  digitalWrite(D1, HIGH);
}

void setup()
{
  //Pins Setup
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(D1, OUTPUT);
  pinMode(D2, OUTPUT);
  pinMode(RELAY_PUMP, OUTPUT);
  pinMode(RELAY_HEATER, OUTPUT);

  double_led_red();
  
  //WiFi Setup (At first will create a hot spot called "mySousVide", you have to connect to it using your pc or smartphone and put wifi credentials). Then the nodemcu will reset and try to connect to the provided wifi, if it fails, the hotspot will be created again.
  //To configure the wifi open the browser at 192.168.4.1
  //The wifi setting will be recorded to persintent memory, so when you turn the device on again, it will try to connect to the same wifi.
  Serial.begin(115200);
  WiFiManager wifiManager;
  wifiManager.setAPCallback(configModeCallback);

  if(!wifiManager.autoConnect("mySousVide")) {
    Serial.println("Failed to connect and hit timeout, reseting ESP...");
    delay(1000);
    ESP.restart();
    delay(1000);
  }
  Serial.println("Wifi Connected!");
  
  //Setup time for the first time
  setupTime();
  
  //Setup Firebase connection
  Firebase.begin(FIREBASE_HOST, FIREBASE_AUTH);
  
  //Get instructions from Firebase
  int settingStatus = 1;
  while(settingStatus == 1){
    settingStatus = getSettingsFromWeb();
    if(settingStatus == 0){
      break;
    }
    delay(2000);
  }

  Serial.println("Firebase OK!");

  //startTemp
  getReliableTemperature();
  
  double_led_green();
  
  //Temperature Sensor
  sensor.begin();
}

void loop()
{
  //Get settings from WEB
  int settingStatus = getSettingsFromWeb();
  if (settingStatus == 0){
    double_led_green();
  }else{
    double_led_red();
  }
  Serial.println("Setting STATUS: ");
  Serial.println(settingStatus);
  Serial.println("\nControllingTemp: ");
  Serial.println(isControllingTemp);
  Serial.println("\nTargetTemp: ");
  Serial.println(targetTemp);
  Serial.println("\nerror: ");
  Serial.println(targetTemp_error);

  if(isControllingTemp){
    if(wasControllingTemp == false){
      if(recordStartTimeIntoWeb() == 0){
        wasControllingTemp = true;
        getReliableTemperature();
        hitTargetTemp = false;
        startTemperature = currentTemperature;
      }
    }else{
      //UpdateTemperature
      updateCurrentTemperature();
    }

    //UpdatePump
    digitalWrite(RELAY_PUMP, HIGH);
    pumpStatus = true;

    //UpdateHeater
    if(heaterStatus){
      if(currentTemperature>(targetTemp + targetTemp_error)){
        digitalWrite(RELAY_HEATER, LOW);
        heaterStatus = false;
        
        if(hitTargetTemp == false){
          if(recordTargetTemperatureHitIntoWeb() == 0){
            hitTargetTemp = true;
          }
        }
        
      }
    }else{
      if(currentTemperature<(targetTemp - targetTemp_error)){
        digitalWrite(RELAY_HEATER, HIGH);
        heaterStatus = true;
      }else{
        if(hitTargetTemp == false){
          if(recordTargetTemperatureHitIntoWeb() == 0){
            hitTargetTemp = true;
          }
        }
      }
    }
    
  }else{
    digitalWrite(RELAY_HEATER, LOW);
    digitalWrite(RELAY_PUMP, LOW);
    heaterStatus = false;
    pumpStatus = false;
    
    if(wasControllingTemp == true){
      if(recordStopTimeIntoWeb() == 0){
        wasControllingTemp = false;
      }
    }
  }
  
  Serial.println(isControllingTemp);
  Serial.println(targetTemp);
  Serial.println(targetTemp_error);
  Serial.println(currentTemperature);

  int settingStatusRecord = recordDataIntoWeb();
  if (settingStatusRecord == 0){
    double_led_green();
  }else{
    double_led_red();
  }
  
  delay(2000);
}

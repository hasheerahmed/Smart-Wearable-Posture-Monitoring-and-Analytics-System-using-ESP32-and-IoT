#define BLYNK_TEMPLATE_ID "TMPL3BFXkOUc3"
#define BLYNK_TEMPLATE_NAME "Posture correction"
#define BLYNK_AUTH_TOKEN "DXqcm0KYGIvD3BhY7L9CYlo89pCZIzxo"
#define BLYNK_PRINT Serial

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <Wire.h>
#include <WiFiClientSecure.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <time.h>

// -------- WIFI --------
char ssid[] = "Hasheer's M35";
char pass[] = "123456789";

String GOOGLE_SCRIPT_ID = "AKfycbyFPjWPoAhmFV4IkpW-OvpaZ4xzsl2st1k05qVtATJoG2T3Hf1iF8Un2070jxnDYNvm-g";

Adafruit_MPU6050 mpu;
BlynkTimer timer;

// -------- PINS --------
#define FLEX_NECK 4
#define FLEX_BACK 5
#define BUZZER 6
#define LED 16
#define BUTTON_PIN 17

// -------- TIMERS --------
unsigned long lastSensorTime = 0;
unsigned long lastReconnect = 0;
unsigned long lastSerialPrint = 0;

#define SENSOR_INTERVAL 30        // Read sensors every 30ms
#define SERIAL_PRINT_INTERVAL 100 // Print to serial every 100ms (10 times per second)

// -------- PITCH FILTERING --------
#define PITCH_SMOOTHING 0.15
float pitchFiltered = 0;

// -------- FLEX READING WITH AVERAGING --------
int readFlex(int pin) {
  long sum = 0;
  for(int i = 0; i < 15; i++) {
    sum += analogRead(pin);
    delay(1);
  }
  return sum / 15;
}

// -------- MPU6050 VARIABLES --------
float pitch = 0;
float pitchOffset = 0;  // Calibrated pitch offset

// -------- NORMALIZE ANGLE FUNCTION --------
float normalizeAngleDiff(float angle) {
  while (angle > 180.0) angle -= 360.0;
  while (angle < -180.0) angle += 360.0;
  return angle;
}

// -------- VARIABLES --------
String currentPosture = "GOOD";
String lastPosture = "GOOD";

int neckBaseline = 0;
int backBaseline = 0;

unsigned long postureStartTime = 0;
unsigned long sessionStartTime = 0;

bool sessionEnded = false;
bool sessionActive = true;

unsigned long goodTime = 0, moderateTime = 0, badTime = 0, veryBadTime = 0;

float finalGP = 0, finalMP = 0, finalBP = 0, finalVP = 0;

String startTimeStr = "";
String endTimeStr = "--";
String lastDuration = "--";

bool lastButtonState = HIGH;

// -------- ENCODE URL --------
String encode(String s){
  s.replace(" ", "%20");
  s.replace(":", "%3A");
  s.replace("+", "%2B");
  return s;
}

// -------- TIME --------
String getTimeString() {
  time_t now = time(nullptr);
  struct tm *t = localtime(&now);
  char buf[30];
  strftime(buf, sizeof(buf), "%I:%M:%S %p", t);
  return String(buf);
}

// -------- GOOGLE SHEETS --------
void sendToGoogleSheets(float gP, float mP, float bP, float vP, String duration){

  if(WiFi.status() != WL_CONNECTED){
    Serial.println("WiFi not connected - cannot send to Google Sheets");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure(); // Skip certificate validation

  Serial.println("Connecting to Google Sheets...");

  if (!client.connect("script.google.com", 443)) {
    Serial.println("Google connection failed");
    return;
  }

  String url = "/macros/s/" + GOOGLE_SCRIPT_ID + "/exec";
  url += "?start=" + encode(startTimeStr);
  url += "&end=" + encode(endTimeStr);
  url += "&duration=" + encode(duration);
  url += "&good=" + String(gP, 2);
  url += "&moderate=" + String(mP, 2);
  url += "&bad=" + String(bP, 2);
  url += "&verybad=" + String(vP, 2);
  url += "&goodTime=" + String(goodTime / 1000);
  url += "&modTime=" + String(moderateTime / 1000);
  url += "&badTime=" + String(badTime / 1000);
  url += "&vbadTime=" + String(veryBadTime / 1000);

  Serial.println("Sending GET request to:");
  Serial.println("https://script.google.com" + url);

  client.print(String("GET ") + url + " HTTP/1.1\r\n");
  client.print("Host: script.google.com\r\n");
  client.print("User-Agent: ESP32\r\n");
  client.print("Connection: close\r\n\r\n");

  Serial.println("Request sent, waiting for response...");

  unsigned long timeout = millis();
  while (client.connected() && millis() - timeout < 5000) {
    if (client.available()) {
      String line = client.readStringUntil('\n');
      Serial.println(line);
    }
  }

  Serial.println("Google Sheets request complete");
  client.stop();
}

// -------- CALIBRATION --------
void calibrate(){
  Serial.println("========================================");
  Serial.println("CALIBRATION: Sit straight in GOOD posture");
  Serial.println("Calibrating in 5 seconds...");
  Serial.println("========================================");
  
  delay(5000);

  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  // Simple pitch offset - single reading
  pitchOffset = atan2(a.acceleration.y, -a.acceleration.z) * 180 / PI;
  
  // Initialize filtered pitch
  pitchFiltered = pitchOffset;

  neckBaseline = readFlex(FLEX_NECK);
  backBaseline = readFlex(FLEX_BACK);

  Serial.println("========================================");
  Serial.println("CALIBRATION COMPLETE");
  Serial.print("Pitch Offset: "); Serial.println(pitchOffset, 2);
  Serial.print("Neck Baseline: "); Serial.println(neckBaseline);
  Serial.print("Back Baseline: "); Serial.println(backBaseline);
  Serial.println("========================================");
}

// -------- UPDATE TIME --------
void updatePostureTime(String posture, unsigned long duration){
  if(posture == "GOOD") goodTime += duration;
  else if(posture == "MODERATE") moderateTime += duration;
  else if(posture == "BAD") badTime += duration;
  else if(posture == "VERY BAD") veryBadTime += duration;
}

// -------- LIVE PERCENTAGE CALCULATION --------
void getLivePercentages(float &gP, float &mP, float &bP, float &vP) {
  // Calculate current posture elapsed time
  unsigned long currentPostureElapsed = millis() - postureStartTime;
  
  // Copy existing accumulated times
  unsigned long liveGoodTime = goodTime;
  unsigned long liveModerateTime = moderateTime;
  unsigned long liveBadTime = badTime;
  unsigned long liveVeryBadTime = veryBadTime;
  
  // Add current posture's ongoing time
  if(currentPosture == "GOOD") 
    liveGoodTime += currentPostureElapsed;
  else if(currentPosture == "MODERATE") 
    liveModerateTime += currentPostureElapsed;
  else if(currentPosture == "BAD") 
    liveBadTime += currentPostureElapsed;
  else if(currentPosture == "VERY BAD") 
    liveVeryBadTime += currentPostureElapsed;
  
  // Calculate percentages
  unsigned long total = liveGoodTime + liveModerateTime + liveBadTime + liveVeryBadTime;
  
  if(total > 0) {
    gP = (liveGoodTime * 100.0) / total;
    mP = (liveModerateTime * 100.0) / total;
    bP = (liveBadTime * 100.0) / total;
    vP = (liveVeryBadTime * 100.0) / total;
  } else {
    gP = 0; mP = 0; bP = 0; vP = 0;
  }
}

// -------- END SESSION --------
void endSession(){

  Serial.println("========================================");
  Serial.println("ENDING SESSION...");

  if(sessionEnded) {
    Serial.println("Session already ended!");
    return;
  }

  // Update final posture time
  updatePostureTime(currentPosture, millis() - postureStartTime);

  unsigned long total = goodTime + moderateTime + badTime + veryBadTime;
  
  if(total == 0) {
    Serial.println("No data to save (total time = 0)");
    return;
  }

  finalGP = (goodTime * 100.0) / total;
  finalMP = (moderateTime * 100.0) / total;
  finalBP = (badTime * 100.0) / total;
  finalVP = (veryBadTime * 100.0) / total;

  endTimeStr = getTimeString();

  int totalSec = (millis() - sessionStartTime) / 1000;
  int hours = totalSec / 3600;
  int minutes = (totalSec % 3600) / 60;
  int seconds = totalSec % 60;
  
  lastDuration = String(hours) + ":" + 
                 (minutes < 10 ? "0" : "") + String(minutes) + ":" + 
                 (seconds < 10 ? "0" : "") + String(seconds);

  Serial.println("Session Summary:");
  Serial.print("Good: "); Serial.print(finalGP, 1); Serial.print("% ("); Serial.print(goodTime / 1000); Serial.println("s)");
  Serial.print("Moderate: "); Serial.print(finalMP, 1); Serial.print("% ("); Serial.print(moderateTime / 1000); Serial.println("s)");
  Serial.print("Bad: "); Serial.print(finalBP, 1); Serial.print("% ("); Serial.print(badTime / 1000); Serial.println("s)");
  Serial.print("Very Bad: "); Serial.print(finalVP, 1); Serial.print("% ("); Serial.print(veryBadTime / 1000); Serial.println("s)");
  Serial.print("Duration: "); Serial.println(lastDuration);

  sendToGoogleSheets(finalGP, finalMP, finalBP, finalVP, lastDuration);

  sessionEnded = true;
  sessionActive = false;

  Serial.println("Session data saved!");
  Serial.println("========================================");
}

// -------- BLYNK UPDATE TIMER --------
void updateBlynk() {
  if(!Blynk.connected()) return;

  if(sessionActive) {
    float gP = 0, mP = 0, bP = 0, vP = 0;
    
    // Get real-time percentages including current ongoing posture
    getLivePercentages(gP, mP, bP, vP);

    Blynk.virtualWrite(V0, gP);
    Blynk.virtualWrite(V1, mP);
    Blynk.virtualWrite(V2, bP);
    Blynk.virtualWrite(V3, vP);
    Blynk.virtualWrite(V4, "ACTIVE");
    Blynk.virtualWrite(V5, currentPosture);
    Blynk.virtualWrite(V6, startTimeStr);
    Blynk.virtualWrite(V7, "--");
    Blynk.virtualWrite(V8, "--");
  } else {
    Blynk.virtualWrite(V0, finalGP);
    Blynk.virtualWrite(V1, finalMP);
    Blynk.virtualWrite(V2, finalBP);
    Blynk.virtualWrite(V3, finalVP);
    Blynk.virtualWrite(V4, "INACTIVE");
    Blynk.virtualWrite(V5, "--");
    Blynk.virtualWrite(V6, startTimeStr);
    Blynk.virtualWrite(V7, endTimeStr);
    Blynk.virtualWrite(V8, lastDuration);
  }
}

// -------- SETUP --------
void setup(){

  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n========================================");
  Serial.println("POSTURE CORRECTION SYSTEM STARTING...");
  Serial.println("========================================");

  Wire.begin(19, 20);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  pinMode(BUZZER, OUTPUT);
  pinMode(LED, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Test outputs
  digitalWrite(LED, HIGH);
  tone(BUZZER, 1000);
  delay(200);
  noTone(BUZZER);
  digitalWrite(LED, LOW);

  Serial.println("Initializing MPU6050...");
  if (!mpu.begin()) {
    Serial.println("Failed to find MPU6050 chip");
    while (1) {
      delay(10);
    }
  }
  Serial.println("MPU6050 Found!");

  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, pass);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if(WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi Connection Failed!");
  }

  Serial.println("Connecting to Blynk...");
  Blynk.config(BLYNK_AUTH_TOKEN);
  Blynk.connect();

  if(Blynk.connected()) {
    Serial.println("Blynk Connected!");
  } else {
    Serial.println("Blynk Connection Failed - will retry");
  }

  // Setup Blynk timer to update every 1 second
  timer.setInterval(1000L, updateBlynk);

  Serial.println("Configuring time (NTP)...");
  configTime(19800, 0, "pool.ntp.org", "time.nist.gov");
  
  // Wait for time to be set
  int timeAttempts = 0;
  while(time(nullptr) < 100000 && timeAttempts < 10) {
    delay(500);
    Serial.print(".");
    timeAttempts++;
  }
  Serial.println("\nTime configured");

  calibrate();

  postureStartTime = millis();
  sessionStartTime = millis();
  startTimeStr = getTimeString();

  Serial.println("========================================");
  Serial.println("SYSTEM READY!");
  Serial.println("Session started at: " + startTimeStr);
  Serial.println("Press button to end session");
  Serial.println("========================================\n");
}

// -------- LOOP --------
void loop(){

  static unsigned long lastPress = 0;

  // -------- BUTTON HANDLING --------
  bool currentButtonState = digitalRead(BUTTON_PIN);

  if (lastButtonState == HIGH && currentButtonState == LOW && millis() - lastPress > 300) {

    lastPress = millis();

    Serial.println("\n*** BUTTON PRESSED ***");

    if (!sessionEnded) {
      endSession();
      updateBlynk(); // Force immediate Blynk update
    }
  }

  lastButtonState = currentButtonState;

  // -------- BLYNK CONNECTION --------
  if (WiFi.status() == WL_CONNECTED) {

    if (!Blynk.connected() && millis() - lastReconnect > 10000) {
      lastReconnect = millis();
      Serial.println("Attempting Blynk reconnect...");
      
      if(Blynk.connect(3000)) {
        Serial.println("Blynk reconnected!");
      } else {
        Serial.println("Blynk reconnect failed");
      }
    }

    if(Blynk.connected()) {
      Blynk.run();
    }
  }

  // Timer must run constantly
  timer.run();

  unsigned long now = millis();

  // -------- SENSOR READING (only if session active) --------
  if(sessionActive && now - lastSensorTime >= SENSOR_INTERVAL){

    lastSensorTime = now;

    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);

    // Calculate raw pitch using simple formula
    pitch = atan2(a.acceleration.y, -a.acceleration.z) * 180 / PI;

    // Apply exponential moving average filter to pitch
    pitchFiltered = (1 - PITCH_SMOOTHING) * pitchFiltered + PITCH_SMOOTHING * pitch;

    // Calculate corrected pitch by subtracting offset
    float correctedPitch = pitchFiltered - pitchOffset;

    // Normalize angle to prevent boundary spikes
    correctedPitch = normalizeAngleDiff(correctedPitch);

    // Read flex sensors with averaging
    int neckDiff = readFlex(FLEX_NECK) - neckBaseline;
    int backDiff = readFlex(FLEX_BACK) - backBaseline;
    int flexDiff = max(abs(neckDiff), abs(backDiff));

    // Determine posture based on corrected pitch and flex sensors
    if (abs(correctedPitch) < 8 && flexDiff < 30) 
      currentPosture = "GOOD";
    else if (abs(correctedPitch) < 18 && flexDiff < 60) 
      currentPosture = "MODERATE";
    else if (abs(correctedPitch) < 28 && flexDiff < 100) 
      currentPosture = "BAD";
    else 
      currentPosture = "VERY BAD";

    // Update time tracking when posture changes
    if(currentPosture != lastPosture){
      updatePostureTime(lastPosture, now - postureStartTime);
      postureStartTime = now;
      lastPosture = currentPosture;
    }

    // LED indicator
    digitalWrite(LED, currentPosture != "GOOD");

    // Buzzer alerts
    if(currentPosture == "BAD") {
      tone(BUZZER, 2000);
    }
    else if(currentPosture == "VERY BAD") {
      tone(BUZZER, 4000);
    }
    else {
      noTone(BUZZER);
    }

    // Print to Serial at reduced rate (10 times per second)
    if(now - lastSerialPrint >= SERIAL_PRINT_INTERVAL) {
      lastSerialPrint = now;
      
      Serial.print(" | CorrectedPitch: "); Serial.print(correctedPitch, 1);
      Serial.print(" | Neck: "); Serial.print(neckDiff);
      Serial.print(" | Back: "); Serial.print(backDiff);
      Serial.print(" | Flex: "); Serial.print(flexDiff);
      Serial.print(" | Posture: "); Serial.print(currentPosture);
      Serial.print(" | Blynk: "); Serial.println(Blynk.connected() ? "Connected" : "Disconnected");
    }
  }
}

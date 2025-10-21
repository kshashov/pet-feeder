#include <Stepper.h>
#include <Wire.h>
#include <Preferences.h>
#include <Stepper.h>
#include <AccelStepper.h>
#include <WiFi.h>
#include <time.h>
#include <WebServer.h>
#include <ArduinoJson.h>

#define LED_R 27
#define LED_G 14
#define LED_B 12

#define motorStep 17
#define motorDir 16
#define motorEnable 23
AccelStepper stepper(1, motorStep, motorDir);

#define SHORT_PRESS_TIME 1000 // 1000 milliseconds
#define BUTTON_PIN       25  // GPIO21 pin connected to button

WebServer server(80);

// flash
const uint32_t NVM_Offset = 0x290000;      // Offset Value For NVS Partition

Preferences preferences;

// button
int lastState = LOW;  // the previous state from the input pin
int currentState;     // the current reading from the input pin
unsigned long pressedTime  = 0;
unsigned long releasedTime = 0;

unsigned long hostLiveMs = 0;

// business logic
enum mode {
  error, 
  idle,                                  // no work
  feeding,                               // 
 // client,                                 // internet work
  host,                                 // wifi host
};

const int maxAlarms = 10;  // Max number of alarms
long storedTimestamps[maxAlarms];
bool alarmedFlags[maxAlarms] = {false};
int alarmSizes[maxAlarms];
bool shouldResetFlags = true;
int numAlarms = 0;

mode state = error; 

const char* prefsNamespace = "alarms";
const char* timeKey = "alarms";
const char* flagKey = "alarmed_flags";
const char* ssidKey = "ssid";
const char* passwordKey = "password";
const char* sizeKey = "alarm_size";


// const unsigned long serverDuration_ms = 10 * 60 * 1000; // 10 minutes
// unsigned long serverStartMillis = 0;
// bool serverActive = false;

// GMT+4 timezone offset in seconds
const long gmtOffset_sec = 4 * 3600;  // 4 hours * 3600 seconds
// Daylight saving time (DST) is not considered here. Adjust if needed.
const int daylightOffset_sec = 0;     // Set to 3600 if DST applies

void setup() {
  // Serial monitor
  Serial.begin(115200); 
  while (!Serial); delay(50);       // start serial comms
  Serial.println("\n\n\nStarting\n");

  // init led
  pinMode (LED_R, OUTPUT);
  pinMode (LED_G, OUTPUT);
  pinMode (LED_B, OUTPUT);
  led(0, 0, 0);

  // init stepper
  stepper.setSpeed(500);
  stepper.setMaxSpeed(700.0);
  stepper.setAcceleration(500.0);
  pinMode(motorEnable, OUTPUT);
  digitalWrite(motorEnable, HIGH);

  // init button  
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  preferences.begin(prefsNamespace, true);
  String savedSSID = preferences.getString(ssidKey, "");
  Serial.println(preferences.getString(ssidKey, ""));
  String savedPassword = preferences.getString(passwordKey, "");
  preferences.end();

  if (connectToWiFi(savedSSID, savedPassword)) {
    // load settings
    loadAlarms();

    // get actual time from internet
    syncTime();

    // start /alarms endpoint
    setupWebServer();
    server.begin();
    Serial.println("start server");
  } else {
    // start wifi host 
    setState(host);
    WiFi.softAP("pet-feeder", "12345678");
    Serial.print("Настройки AP запущены. IP адрес: ");
    Serial.println(WiFi.softAPIP());
    setupHostWebServer();
    server.begin();
    hostLiveMs = millis();
  }
}

void loop() {
  checkAlarms();

  // if (serverActive) {
  server.handleClient();
  // }

  // chech web server
  //if (serverActive && (millis() - serverStartMillis) < serverDuration_ms) {
  //  server.handleClient();
  //} else if (serverActive) {
  //  // Time to stop the server
  //  server.stop();
  //  serverActive = false;
  //  Serial.println("🛑 Web server stopped after 10 minutes.");
  //  state = idle;
  //}

  // try to restart if more than 10 minutes of wifi host
  if (hostLiveMs != 0) {
    if (millis() - hostLiveMs > 10*60*1000) {
      ESP.restart();
    }
  }

  // read the state of the switch/button:
  currentState = digitalRead(BUTTON_PIN);
 if (lastState == HIGH && currentState == LOW) {      // button is pressed
    pressedTime = millis();
 } else if (lastState == LOW && currentState == HIGH) { // button is released
    releasedTime = millis();

    long pressDuration = releasedTime - pressedTime;

    if (pressDuration > 0) {
      if ( pressDuration < SHORT_PRESS_TIME) {
        Serial.println("A short press is detected");
        if (state == error) {
          syncTime();
        } else if (state == idle) {
          feed(1);
        }
      } else {
          Serial.println("A long press is detected");

        // } else {
          // server.stop();
          // serverActive = false;
          // Serial.println("stop server");
        // }
      }
    }
  }

  // save the the last state
  lastState = currentState;
}

void checkAlarms() {
  //if (!alarmsLoaded) return;

  // Get current time in milliseconds
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to get time");
    return;
  }

  // Convert current time to seconds
  int nowSeconds = timeinfo.tm_hour * 3600 + timeinfo.tm_min * 60 + timeinfo.tm_sec;

  // Reset alarms for new day
  if (numAlarms > 0) {
    if (shouldResetFlags && (nowSeconds < storedTimestamps[0])) {
      // Set false for all
      for (int i = 0; i < numAlarms; i++) {
        alarmedFlags[i] = false;
      }
      shouldResetFlags = false;
    }
  }

  for (int i = 0; i < numAlarms; i++) {
    long alarm = storedTimestamps[i];
    long diffMs = nowSeconds - alarm;

    // Trigger if within ±500ms (i.e., same second) and not already alarmed
    if ((diffMs > 0) && (diffMs < 60 * 10) && !alarmedFlags[i]) {
      char buf[60];
      //struct tm* alarmTime = localtime((time_t*)&(storedTimestamps[i] / 1000));
      //strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", alarmTime);

      //Serial.printf("[ALARM TRIGGERED] ID: %d | Time: %s\n", i, buf);

      // Mark as alarmed
      alarmedFlags[i] = true;

      // Save updated flags back to NVS
      preferences.begin(prefsNamespace, false);
      // Save timestamps (as array of long = int32_t or int64_t depending on platform)
      preferences.putBytes(flagKey, (uint8_t*)alarmedFlags, maxAlarms * sizeof(bool));
      preferences.end();
      Serial.println("Alarms and flags saved to NVS");
  
      feed(alarmSizes[i]);
      shouldResetFlags = true;
    }
  }
}


void syncTime() {
  // Init and get time
  configTime(gmtOffset_sec, daylightOffset_sec, "pool.ntp.org", "time.nist.gov");

  Serial.println("Waiting for time...");

  int attempts = 0;
  while (time(nullptr) < 1000000000) { // Wait for valid time
    Serial.print(".");
    delay(200);
    if (attempts > 50) {
      setState(error);
      Serial.println("Failed to synchronize time");
    } else {
      attempts++;
    }
  }
  Serial.println();
  Serial.println("Time synchronized");
  setState(idle);

}

void setupHostWebServer() {
 // Главная страница с формой
  server.on("/", HTTP_GET, []() {
    String html = "<!DOCTYPE html><html><body>";
    html += "<h1>Setup WiFi</h1>";
    html += "<form action='/save' method='POST'>";
    html += "SSID: <input type='text' name='ssid' required><br>";
    html += "Password: <input type='password' name='password' required><br>";
    html += "<input type='submit' value='Save'>";
    html += "</form></body></html>";
    server.send(200, "text/html", html);
  });

  // Обработка отправки формы
  server.on("/save", HTTP_POST, []() {
    String ssid = server.arg("ssid");
    String password = server.arg("password");
    Serial.print("Save wifi: ");
    Serial.println(ssid);

    // Сохраняем данные в NVS
    preferences.begin(prefsNamespace, false);
    preferences.putString(ssidKey, ssid);
    preferences.putString(passwordKey, password);
    preferences.end();

    // Отправляем ответ
    String response = "<!DOCTYPE html><html><body>";
    response += "<h1>Done!</h1>";
    response += "<p>SSID: " + ssid + "</p>";
    response += "<p>Restarting...</p>";
    response += "</body></html>";
    
    server.send(200, "text/html", response);
    delay(2000);
    ESP.restart();
  });

}

void setupWebServer() {
  // Serve a simple status page at root
  server.on("/", HTTP_GET, []() {
    String html = "<h1>ESP32 Pet Feeder API</h1>";
    server.send(200, "text/html", html);
  });

  // Handle POST request to save alarms
  // server.on("/alarms", HTTP_POST, []() {
  //   if (!server.hasArg("plain")) {
  //     server.send(400, "application/json", "{\"error\":\"No data provided\"}");
  //     return;
  //   }

  //   String body = server.arg("plain");
  //   DynamicJsonDocument doc(2048);
  //   DeserializationError error = deserializeJson(doc, body);

  //   if (error) {
  //     Serial.print("JSON parse failed: ");
  //     Serial.println(error.c_str());
  //     server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
  //     return;
  //   }

  //   if (!doc.is<JsonArray>()) {
  //     server.send(400, "application/json", "{\"error\":\"Expected JSON array\"}");
  //     return;
  //   }

  //   int count = 0;
  //   for (JsonObject obj : doc.as<JsonArray>()) {
  //     if (!obj.containsKey("time") || !obj.containsKey("alarmed") || !obj.containsKey("size")) {
  //       continue;
  //     }
  //     storedTimestamps[count] = obj["time"].as<long>();
  //     alarmedFlags[count] = obj["alarmed"].as<bool>();
  //     alarmSizes[count]= obj["size"].as<int>();
  //     count++;
  //     if (count >= maxAlarms) break;
  //   }

  //   numAlarms = count;

  //   // Save to NVS
  //   saveAlarms();
  //   server.send(200, "application/json", "{\"status\":\"Items saved\"}");
  //   Serial.printf("Saved %d alarms\n", count);
  // });

  server.on("/alarms/feed", HTTP_POST, []() {
    String timeStr = server.arg("time");
    String countStr = server.arg("size");
    long count = 1;
    if (countStr != "") {
      count = countStr.toInt();
    }

    if (timeStr != "") {
      long time = timeStr.toInt();
      bool found = false;
      for (int i = 0; i < numAlarms; i++) {
        if (storedTimestamps[i] == time) {
          count = alarmSizes[i];
          alarmedFlags[i] = true;
          found = true;
          break;
        }
      }
      if (!found) {
        server.send(404, "application/json", "{\"status\":\"Not found time " + timeStr + "\"}");
      }
    }

    server.send(200, "application/json", "{\"status\":\"Feeded " + String(count) + " times\"}");
    feed(count);
  });

  server.on("/alarms/skip", HTTP_POST, []() {
    String timeStr = server.arg("time");

    if (timeStr != "") {
      long time = timeStr.toInt();
      bool found = false;
      for (int i = 0; i < numAlarms; i++) {
        if (storedTimestamps[i] == time) {
          alarmedFlags[i] = true;
          found = true;
          break;
        }
      }
      if (!found) {
        server.send(404, "application/json", "{\"status\":\"Not found time " + timeStr + "\"}");
      }
    }

    server.send(200, "application/json", "{\"status\":\"Skipped " + timeStr + " time\"}");
  });


  // GET endpoint to view current alarms
  server.on("/alarms", HTTP_GET, []() {
    DynamicJsonDocument doc(2048);
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < numAlarms; i++) {
      JsonObject obj = arr.createNestedObject();
      obj["time"] = storedTimestamps[i];
      obj["alarmed"] = alarmedFlags[i];
      obj["size"] = alarmSizes[i];
    }
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  });

  // PUT /addAlarm - добавить будильник
  server.on("/alarms", HTTP_PUT, []() {
    DynamicJsonDocument doc(64);
    DeserializationError error = deserializeJson(doc, server.arg("plain"));

    if (error || !doc.containsKey("time")) {
      server.send(400, "application/json", "{\"error\":\"Invalid JSON format or missing 'time' field\"}");
      return;
    }

    int newTime = doc["time"].as<int>();
    int size = doc["size"].as<int>();
    if (newTime < 0 || newTime >= 24*60*60 || size < 1 || size > 99 ) {
      server.send(400, "application/json", "{\"error\":\"Time or rings out of range\"}");
      return;
    }

    // Проверка на дубликат 
    for (int i = 0; i < numAlarms; i++) {
      if (storedTimestamps[i] == newTime) {
        server.send(200, "application/json", "{\"status\":\"Item already exists\"}");
        return;
      }
    }

    // Добавление
    storedTimestamps[numAlarms] = newTime;
    alarmedFlags[numAlarms] = false;
    alarmSizes[numAlarms] = size;
    numAlarms++;

    sortAlarms();
    saveAlarms();
    server.send(200, "application/json", "{\"status\":\"Item added successfully\"}");
  });

  server.on("/alarms", HTTP_PATCH, []() {
    DynamicJsonDocument doc(64);
    DeserializationError error = deserializeJson(doc, server.arg("plain"));

    if (error || !doc.containsKey("time") || !doc.containsKey("size")) {
      server.send(400, "application/json", "{\"error\":\"Invalid JSON format (requires time and size)\"}");
      return;
    }

    if (error || !doc.containsKey("time")) {
      server.send(400, "application/json", "{\"error\":\"Invalid JSON format or missing 'time' field\"}");
      return;
    }

    int time = doc["time"].as<int>();
    int size = doc["size"].as<int>();
    if (size < 1 || size > 99 ) {
      server.send(400, "application/json", "{\"error\":\"Size out of range\"}");
      return;
    }

    // Поиск 
    for (int i = 0; i < numAlarms; i++) {
      if (storedTimestamps[i] == time) {
        alarmSizes[i] = size;
        saveAlarms();
        server.send(200, "application/json", "{\"status\":\"Item has been updated\"}");
        return;
      }
    }

    server.send(404, "application/json", "{\"status\":\"Item not found\"}");
  });

  server.on("/alarms", HTTP_DELETE, []() {
    if (!server.hasArg("time")) {
      server.send(400, "application/json", "{\"error\":\"Missing time query parameter\"}");
      return;
    }

    int timeToRemove = server.arg("time").toInt();
    int indexToRemove = -1;

    for (int i = 0; i < numAlarms; i++) {
      if (storedTimestamps[i] == timeToRemove) {
        indexToRemove = i;
        break;
      }
    }

    if (indexToRemove != -1) {
      // Сдвигаем массив
      for (int i = indexToRemove; i < numAlarms - 1; i++) {
        storedTimestamps[i] = storedTimestamps[i + 1];
        alarmedFlags[i] = alarmedFlags[i + 1];
        alarmSizes[i] = alarmSizes[i + 1];
      }
      numAlarms--;
      saveAlarms();
      server.send(200, "application/json", "{\"status\":\"Item removed successfully\"}");
    } else {
      server.send(404, "application/json", "{\"error\":\"Item not found\"}");
    }
  });
}




void feed(int size) {
  setState(feeding); 
  Serial.println("\nFeed: " + String(size));
  digitalWrite(motorEnable, LOW);
  // stepper.step(-1024);
  stepper.move(-2000*size);
  stepper.runToPosition();
  stepper.run();
  delay(100);
  digitalWrite(motorEnable, HIGH);
  setState(idle);
}

void setState(mode newState) {
  state = newState;
  if (state == error) {
    led(1, 0, 0);
  } else if (state == idle) {
    led(1,1,1); // TODO 000
  } else if (state == feeding) {
    led(0, 0, 1);
  } else if (state == host) {
    led(0, 1, 0);
  }
}

void led(int r, int g, int b) {
  if (r == 0) {
    digitalWrite(LED_R, LOW);
  } else {
    digitalWrite(LED_R, HIGH);
  }

  if (g == 0) {
    digitalWrite(LED_G, LOW);
  } else {
    digitalWrite(LED_G, HIGH);
  }

  if (b == 0) {
    digitalWrite(LED_B, LOW);
  } else {
    digitalWrite(LED_B, HIGH);
  }

}

bool connectToWiFi(String ssid, String password) {
  Serial.print("Connecting to ");
  Serial.print(ssid);
  WiFi.begin(ssid, password);
  for (int i = 0; i < 60; i++) {
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi connected. IP: " + WiFi.localIP().toString());
        return true;
    }
    delay(500);
    Serial.print(".");
  }

  return WiFi.status() == WL_CONNECTED;
}

// ----------------- ALARMS -----------------

// Save timestamps (ms) and flags to NVS
bool saveAlarms() {
  if (numAlarms > maxAlarms) return false;

  preferences.begin(prefsNamespace, false);

  // Save timestamps (as array of long = int32_t or int64_t depending on platform)
  preferences.putBytes(timeKey, (uint8_t*)storedTimestamps, numAlarms * sizeof(long));
  preferences.putBytes(sizeKey, (uint8_t*)alarmSizes, numAlarms * sizeof(int));
  preferences.putBytes(flagKey, (uint8_t*)alarmedFlags, maxAlarms * sizeof(bool));


  preferences.end();
  Serial.println("Alarms and flags saved to NVS");
  return true;
}

// Load timestamps and flags from NVS
void loadAlarms() {
  preferences.begin(prefsNamespace, true);

  // Load timestamps
  size_t timeBytes = preferences.getBytes(timeKey, nullptr, 0);
  if (timeBytes == 0) {
    preferences.end();
  }

  int count = timeBytes / sizeof(long);
  numAlarms = min(count, maxAlarms);

  preferences.getBytes(timeKey, storedTimestamps, timeBytes);

  // Load sizes
  size_t sizeBytes = preferences.getBytes(sizeKey, nullptr, 0);
  if (sizeBytes >= numAlarms * sizeof(int)) {
     preferences.getBytes(sizeKey, alarmSizes, sizeBytes);
  } else {
     // Если поле не найдено (старая версия), ставим 1 звонок по умолчанию
     for(int i=0; i < numAlarms; i++) alarmSizes[i] = 1;
  }

  // Load alarmed flags
  size_t flagBytes = preferences.getBytes(flagKey, nullptr, 0);
  if (flagBytes >= maxAlarms * sizeof(bool)) {
    preferences.getBytes(flagKey, alarmedFlags, flagBytes);
  } else {
    // Initialize if not present
    memset(alarmedFlags, false, sizeof(alarmedFlags));
  }

  preferences.end();
  Serial.println("Alarms and flags loaded from NVS");
  printAllAlarms();
}

void sortAlarms() {
  // ... (unchanged) ...
  for (int i = 0; i < numAlarms - 1; i++) {
    for (int j = 0; j < numAlarms - i - 1; j++) {
      if (storedTimestamps[j] > storedTimestamps[j + 1]) {
        int tempTime = storedTimestamps[j];
        storedTimestamps[j] = storedTimestamps[j + 1];
        storedTimestamps[j + 1] = tempTime;
        
        // Swap Rings
        int tempSize = alarmSizes[j];
        alarmSizes[j] = alarmSizes[j + 1];
        alarmSizes[j + 1] = tempSize;

        bool tempFlag = alarmedFlags[j];
        alarmedFlags[j] = alarmedFlags[j + 1];
        alarmedFlags[j + 1] = tempFlag;
      }
    }
  }
}

void printAllAlarms() {
  Serial.println("Loaded Alarms:");
  for (int i = 0; i < numAlarms; i++) {
    char buf[60];
    //struct tm* tmPtr = localtime((time_t*)&(storedTimestamps[i] / 1000));
    //strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tmPtr);
    //Serial.printf("  [%d] %s - Alarmed: %s\n", i, buf, alarmedFlags[i] ? "Yes" : "No");
  }
}

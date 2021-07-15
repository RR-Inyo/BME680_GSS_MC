/*
   BME680_GSS.ino, BME680 Google Spreadsheet recorder, multicore
   (c) 2021 @RR_Inyo
   Released under 
*/

#include <M5Stack.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "bsec.h"

/*
   Global variables and objects
*/
// Declare helper functions for BME680
void checkIaqSensorStatus(void);
void errLcd(void);

// Declare WiFi constants
void connectWiFi(void);
const char* ssid = "XXXXXXXXXX";
const char* password = "YYYYYYYYYY";

// Declare NTP servers and the setting constants
const char* server1 = "ntp.nict.jp";
const char* server2 = "time.google.com";
const char* server3 = "ntp.jst.mfeed.ad.jp";
const long JST = 3600L * 9;
const int summertime = 0;

// Declare display functions and global constants
void showFrame(void);
void showBatt(void);
void showClock(void);
void showIPaddress(void);
void showHTTPCode(void);
void showIAQAccuracy(void);

// Declare a function to report data to Google Spreadsheet
void reportGSS(void* arg);
const char* apiURL = "https://script.google.com/macros/s/ZZZZZZZZZZ/exec";
int httpCode;

// Obtain handler for BME680
Bsec iaqSensor;

// Obtain HTTP client handler
HTTPClient http;

/*
   The setup function, run once
*/
void setup(void)
{
  String output;

  // Initialize M5Stack
  M5.begin();
  M5.Power.begin();

  // Mute speaker
  M5.Speaker.begin();
  M5.Speaker.mute();

  // Initialize I2C
  Wire.begin();

  // Initialize text size and cursor position
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(0, 0, 1);

  // Initialize BME60 sensor
  iaqSensor.begin(BME680_I2C_ADDR_SECONDARY, Wire);
  output = "\nBSEC library version " + String(iaqSensor.version.major) + "." + String(iaqSensor.version.minor) + "." + String(iaqSensor.version.major_bugfix) + "." + String(iaqSensor.version.minor_bugfix);
  M5.Lcd.println(output);
  checkIaqSensorStatus();

  bsec_virtual_sensor_t sensorList[10] = {
    BSEC_OUTPUT_RAW_TEMPERATURE,
    BSEC_OUTPUT_RAW_PRESSURE,
    BSEC_OUTPUT_RAW_HUMIDITY,
    BSEC_OUTPUT_RAW_GAS,
    BSEC_OUTPUT_IAQ,
    BSEC_OUTPUT_STATIC_IAQ,
    BSEC_OUTPUT_CO2_EQUIVALENT,
    BSEC_OUTPUT_BREATH_VOC_EQUIVALENT,
    BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
    BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,
  };

  iaqSensor.updateSubscription(sensorList, 10, BSEC_SAMPLE_RATE_LP);
  checkIaqSensorStatus();

  // Connect to WiFi
  connectWifi();

  // Synchronize to NTP server
  configTime(JST, summertime, server1, server2, server3);

  // Show display frame
  showFrame();

  // Run a task on Core 1
  xTaskCreatePinnedToCore(reportGSS, "reportGSS", 8192, NULL, 1, NULL, 1);
}

/*
   The loop function, run repeatedly
*/
void loop(void)
{
  struct tm tm;
  static int minute_old;
  unsigned long time_trigger = millis();

  if (iaqSensor.run()) { // If new data is available
    // Show data on LCD
    showData();
  } else {
    // Check BME680 status
    checkIaqSensorStatus();
  }

  // Show date and time
  showClock();

  // Show IP adress
  showIPAddress();

  // Show HTTP code
  showHTTPCode();

  // Show IAQ accuracy
  showIAQAccuracy();

  // Show battery level
  showBatt();
}

/*
   The helper function for BME680
*/
void checkIaqSensorStatus(void)
{
  String output;
  if (iaqSensor.status != BSEC_OK) {
    if (iaqSensor.status < BSEC_OK) {
      output = "BSEC error code : " + String(iaqSensor.status);
      errLcd(); /* Halt in case of failure */
    } else {
      output = "BSEC warning code : " + String(iaqSensor.status);
      M5.Lcd.setCursor(0, 224, 1);
      M5.Lcd.println(output);
    }
  }

  if (iaqSensor.bme680Status != BME680_OK) {
    if (iaqSensor.bme680Status < BME680_OK) {
      output = "BME680 error code : " + String(iaqSensor.bme680Status);
      errLcd(); /* Halt in case of failure */
    } else {
      output = "BME680 warning code : " + String(iaqSensor.bme680Status);
      M5.Lcd.setCursor(160, 224, 1);
      M5.Lcd.println(output);
    }
  }
}

/*
   The error function for BME680, never return
*/
void errLcd(void)
{
  while (true) {
    M5.Lcd.setTextColor(WHITE, BLACK);
    M5.Lcd.setCursor(80, 100, 4);
    M5.Lcd.println("BME680 Error!");
    delay(500);
    M5.Lcd.setTextColor(RED, BLACK);
    M5.Lcd.setCursor(80, 100, 4);
    M5.Lcd.println("BME680 Error!");
    delay(500);
  }
}

/*
   WiFi connection function
*/
void connectWifi(void) {
  WiFi.begin(ssid, password);
  M5.Lcd.println("Connecting to:");
  M5.Lcd.println(ssid);
  while (WiFi.status() != WL_CONNECTED); {
    delay(50);
    M5.Lcd.print('.');
  }
  M5.Lcd.print("\r\nWifi connected!\r\nIP address: ");
  M5.Lcd.println(WiFi.localIP());
  delay(100);
}

/*
   Show wallpaper and frame to display BME680 data on LCD
*/
void showFrame(void)
{
  // Clear LCD
  M5.Lcd.fillScreen(BLACK);

  // Show wallpaper
  M5.Lcd.drawJpgFile(SD, "/wall.jpg");
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(WHITE, BLACK);

  // Show temperature and humidity frame
  M5.Lcd.setCursor(8, 8, 1);
  M5.Lcd.print("Temperature [ C]");
  M5.Lcd.setCursor(8 + 6 * 13, 5);
  M5.Lcd.print("o");

  M5.Lcd.setCursor(168, 8, 1);
  M5.Lcd.print("Humidity [%]");

  // Show air pressure and gas resistance frame
  M5.Lcd.setCursor(8, 88, 1);
  M5.Lcd.print("Pressure [hPa]");

  M5.Lcd.setCursor(168, 88, 1);
  M5.Lcd.print("Gas resistance [kohm]");

  // Show CO2 equivalent and static IAQ
  M5.Lcd.setCursor(8, 168, 1);
  M5.Lcd.print("CO  equivalent [ppm]");
  M5.Lcd.setCursor(8 + 6 * 2, 171);
  M5.Lcd.print("2");

  M5.Lcd.setCursor(168, 168, 1);
  M5.Lcd.print("Static IAQ");
}

/*
   Show data on LCD
*/
void showData(void)
{
  int col;
  M5.Lcd.setTextSize(1);

  // Show temperature
  if (iaqSensor.temperature >= 30) {
    col = RED;
  } else {
    col = WHITE;
  }
  M5.Lcd.setTextColor(col, BLACK);
  M5.Lcd.setCursor(16, 28, 4);
  M5.Lcd.printf("%2.2f ", iaqSensor.temperature);

  // Show humidity
  if (iaqSensor.humidity >= 70) {
    col = RED;
  } else {
    col = WHITE;
  }
  M5.Lcd.setTextColor(col, BLACK);
  M5.Lcd.setCursor(178, 28, 4);
  M5.Lcd.printf("%02.2f", iaqSensor.humidity);

  // Show pressure
  if (iaqSensor.pressure <= 99000) {
    col = RED;
  } else {
    col = WHITE;
  }
  M5.Lcd.setTextColor(col, BLACK);
  M5.Lcd.setCursor(16, 108, 4);
  M5.Lcd.printf("%04.2f", iaqSensor.pressure / 100);

  // Show gas resistance
  if (iaqSensor.gasResistance <= 300000) {
    col = RED;
  } else {
    col = WHITE;
  }
  M5.Lcd.setTextColor(col, BLACK);
  M5.Lcd.setCursor(176, 108, 4);
  M5.Lcd.printf("%04.2f", iaqSensor.gasResistance / 1000);

  // Show CO2 equivalent
  if (iaqSensor.co2Equivalent >= 1000) {
    col = RED;
  } else {
    col = WHITE;
  }
  M5.Lcd.setTextColor(col, BLACK);
  M5.Lcd.setCursor(16, 188, 4);
  M5.Lcd.printf("%03.2f", iaqSensor.co2Equivalent);

  // Show static IAQ
  if (iaqSensor.staticIaq >= 110) {
    col = RED;
  } else {
    col = WHITE;
  }
  M5.Lcd.setTextColor(col, BLACK);
  M5.Lcd.setCursor(176, 188, 4);
  M5.Lcd.printf("%03.2f", iaqSensor.staticIaq);
}

/*
   Show battery level on upper right corner of LCD
*/
void showBatt(void)
{
  static bool first = true;
  static int battLevel;
  static int battLevelOld;

  // Get battery level
  battLevel = M5.Power.getBatteryLevel();

  // Do not update when battery level has not changed.
  if (battLevel == battLevelOld && !first) {
    first = false;
    return;
  }

  // Show numeric
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(270, 2, 1);
  M5.Lcd.printf("%3d%%", battLevel);

  // Show blocks
  // 100%
  if (battLevel > 99) {
    M5.Lcd.fillRect(265 + 6 * 5, 2, 5, 8, WHITE);
  } else {
    M5.Lcd.fillRect(265 + 6 * 5, 2, 5, 8, BLACK);
    M5.Lcd.drawRect(265 + 6 * 5, 2, 5, 8, WHITE);
  }

  // 75%
  if (battLevel > 74) {
    M5.Lcd.fillRect(265 + 6 * 6, 2, 5, 8, WHITE);
  } else {
    M5.Lcd.fillRect(265 + 6 * 6, 2, 5, 8, BLACK);
    M5.Lcd.drawRect(265 + 6 * 6, 2, 5, 8, WHITE);
  }

  // 50%
  if (battLevel > 49) {
    M5.Lcd.fillRect(265 + 6 * 7, 2, 5, 8, WHITE);
  } else {
    M5.Lcd.fillRect(265 + 6 * 7, 2, 5, 8, BLACK);
    M5.Lcd.drawRect(265 + 6 * 7, 2, 5, 8, WHITE);
  }

  // 25%
  if (battLevel > 24) {
    M5.Lcd.fillRect(265 + 6 * 8, 2, 5, 8, WHITE);
  } else {
    M5.Lcd.drawRect(265 + 6 * 8, 2, 5, 8, WHITE);
  }

  // Preserve bettery level
  first = false;
  battLevelOld = battLevel;
}

/*
   Show digital clock on lower left corner of LCD
*/
void showClock(void)
{
  struct tm tm;
  if (getLocalTime(&tm)) {
    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(0, 232, 1);
    M5.Lcd.printf("%d-%02d-%02d %02d:%02d",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min);
  }
}

/*
   Show IP address
*/
void showIPAddress(void)
{
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(110, 232, 1);
  M5.Lcd.print("IP:");
  M5.Lcd.print(WiFi.localIP());
}

/*
   Show HTTP code
*/
void showHTTPCode(void)
{
  // Show response on LCD
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(220, 232, 1);
  M5.Lcd.printf("HTTP:%d", httpCode);
}

/*
   Show IAQ accuracy
*/
void showIAQAccuracy(void)
{
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(314, 232, 1);
  M5.Lcd.printf("%d", iaqSensor.iaqAccuracy);
}

/*
   Report BME680 data to Google Spreadsheet via API using JSON
   To be assigned to Core 1
*/
void reportGSS(void* arg)
{
  struct tm tm;
  int minute_old;
  char datebuf[32];
  char pubMessage[512];

  // Infinate loop running on Core 1
  while (true) {
    // Report to Google Spreadsheet every minute
    if (getLocalTime(&tm)) {
      if (minute_old == tm.tm_min) {
        continue;
      }
    }

    // Preserve present minute valuewf wwa
    minute_old = tm.tm_min;

    // Create date-time text
    if (getLocalTime(&tm)) {
      sprintf(datebuf, "%d-%02d-%02d %02d:%02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    }

    // Create JSON message
    StaticJsonDocument<500> doc;
    JsonObject object = doc.to<JsonObject>();

    object["date"] = datebuf;
    object["temperature"] = iaqSensor.temperature;
    object["humidity"] = iaqSensor.humidity;
    object["pressure"] = iaqSensor.pressure / 100;
    object["gasResistance"] = iaqSensor.gasResistance / 1000;
    object["iaq"] = iaqSensor.iaq;
    object["iaqAccuracy"] = iaqSensor.iaqAccuracy;
    object["staticIAQ"] = iaqSensor.staticIaq;
    object["co2Equivalent"] = iaqSensor.co2Equivalent;
    object["vocEquivalent"] = iaqSensor.breathVocEquivalent;

    serializeJson(doc, pubMessage);

    // Report to Google Spreadsheet API
    http.begin(apiURL);
    httpCode = http.POST(pubMessage);
  }
}

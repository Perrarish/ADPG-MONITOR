
/*
  ADPG - TURBINE, BATTERY AND PHONE MONITORING
  ESP32 DevKit V1

  COMMUNICATION:
  ESP32 hosts a local HTTP server.
  Python and browser dashboard fetch JSON using ESP32 IP.

  API:
  http://ESP32_IP/api/data

  SENSORS:
  INA219 Generator : 0x40
  INA219 Battery   : 0x41
  INA219 Phone     : 0x44
  DS18B20 x 3      : GPIO 4
  A3144 Hall       : GPIO 27
  SDP810           : 0x25
  microSD          : SPI
*/

// =====================================================
// LIBRARIES
// =====================================================

#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_INA219.h>
#include <SensirionI2CSdp.h>
#include <math.h>

// =====================================================
// WIFI CONFIGURATION
// =====================================================

const char* WIFI_SSID = "Redmi Note 11";
const char* WIFI_PASSWORD = "12345678";

// =====================================================
// PIN CONFIGURATION
// =====================================================

#define SDA_PIN       21
#define SCL_PIN       22

#define TEMP_PIN      4
#define HALL_PIN      27

#define SD_CS         5
#define SD_SCK        18
#define SD_MISO       19
#define SD_MOSI       23

// =====================================================
// SENSOR OBJECTS
// =====================================================

Adafruit_INA219 generatorINA(0x40);
Adafruit_INA219 batteryINA(0x41);
Adafruit_INA219 phoneINA(0x44);

OneWire oneWire(TEMP_PIN);
DallasTemperature tempSensors(&oneWire);

SensirionI2CSdp sdp;

SPIClass sdSPI(VSPI);
File dataFile;

WebServer server(80);

// =====================================================
// CONFIGURATION
// =====================================================

const float PULSES_PER_REV = 1.0;
const unsigned long SAMPLE_INTERVAL = 1000;

volatile unsigned long hallPulses = 0;

bool generatorINA_OK = false;
bool batteryINA_OK = false;
bool phoneINA_OK = false;
bool pressure_OK = false;
bool sd_OK = false;

unsigned long previousSampleTime = 0;

// Latest sensor readings

float rpm = NAN;

float generatorVoltage = NAN;
float generatorCurrent = NAN;
float generatorPower = NAN;

float batteryTemp = NAN;
float bottomTemp = NAN;
float topTemp = NAN;
float temperatureDifference = NAN;

float batteryVoltage = NAN;
float batteryCurrent = NAN;
float batteryPower = NAN;

String batteryStatus = "ERROR";

float phoneVoltage = NAN;
float phoneCurrent = NAN;
float phonePower = NAN;

float pressure = NAN;
float pressureTemperature = NAN;

unsigned long latestTime = 0;

// =====================================================
// HALL SENSOR INTERRUPT
// =====================================================

void IRAM_ATTR countHallPulse() {
  hallPulses++;
}

// =====================================================
// JSON NUMBER HELPER
// =====================================================

void addJsonNumber(
  String &json,
  const char* key,
  float value,
  int decimals = 2
) {
  json += "\"";
  json += key;
  json += "\":";

  if (isfinite(value)) {
    json += String(value, decimals);
  } else {
    json += "null";
  }
}

// =====================================================
// CREATE JSON RESPONSE
// =====================================================

String createJSON() {

  String json;
  json.reserve(700);

  json = "{";

  json += "\"time_ms\":";
  json += String(latestTime);
  json += ",";

  addJsonNumber(json, "rpm", rpm, 1);
  json += ",";

  addJsonNumber(json, "generator_voltage", generatorVoltage);
  json += ",";

  addJsonNumber(json, "generator_current", generatorCurrent);
  json += ",";

  addJsonNumber(json, "generator_power", generatorPower);
  json += ",";

  addJsonNumber(json, "battery_temp", batteryTemp);
  json += ",";

  addJsonNumber(json, "bottom_temp", bottomTemp);
  json += ",";

  addJsonNumber(json, "top_temp", topTemp);
  json += ",";

  addJsonNumber(json, "temperature_difference", temperatureDifference);
  json += ",";

  addJsonNumber(json, "battery_voltage", batteryVoltage);
  json += ",";

  addJsonNumber(json, "battery_current", batteryCurrent);
  json += ",";

  addJsonNumber(json, "battery_power", batteryPower);
  json += ",";

  json += "\"battery_status\":\"";
  json += batteryStatus;
  json += "\",";

  addJsonNumber(json, "phone_voltage", phoneVoltage);
  json += ",";

  addJsonNumber(json, "phone_current", phoneCurrent);
  json += ",";

  addJsonNumber(json, "phone_power", phonePower);
  json += ",";

  addJsonNumber(json, "pressure", pressure);
  json += ",";

  addJsonNumber(json, "pressure_temperature", pressureTemperature);

  json += "}";

  return json;
}

// =====================================================
// WEB SERVER ROUTES
// =====================================================

void handleRoot() {

  server.send(
    200,
    "text/plain",
    "ADPG ESP32 Sensor Server Running"
  );
}

// Sensor JSON endpoint with CORS enabled

void handleSensorData() {

  String json = createJSON();

  server.sendHeader(
    "Access-Control-Allow-Origin",
    "*"
  );

  server.send(
    200,
    "application/json",
    json
  );
}

void handleNotFound() {

  server.send(
    404,
    "application/json",
    "{\"error\":\"Endpoint not found\"}"
  );
}

// =====================================================
// WIFI INITIALIZATION
// =====================================================

void connectWiFi() {

  Serial.println("\n[WiFi] Connecting...");

  WiFi.mode(WIFI_STA);

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );

  unsigned long startTime = millis();

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - startTime < 20000
  ) {

    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {

    Serial.println("\n[WiFi] Connected");

    Serial.print("[WiFi] ESP32 IP Address: ");
    Serial.println(WiFi.localIP());

    Serial.println("[WiFi] Server port: 80");

    Serial.print("[WiFi] JSON URL: http://");
    Serial.print(WiFi.localIP());
    Serial.println("/api/data");

  } else {

    Serial.println("\n[WiFi] Connection failed");
  }
}

// =====================================================
// TEMPERATURE SENSOR ADDRESSES
// =====================================================

void printTemperatureAddresses() {

  int count = tempSensors.getDeviceCount();

  Serial.println("\nDS18B20 SENSOR ADDRESSES");

  for (int i = 0; i < count; i++) {

    DeviceAddress address;

    if (tempSensors.getAddress(address, i)) {

      Serial.print("Sensor ");
      Serial.print(i + 1);
      Serial.print(": ");

      for (uint8_t j = 0; j < 8; j++) {

        if (address[j] < 16) {
          Serial.print("0");
        }

        Serial.print(address[j], HEX);
      }

      Serial.println();
    }
  }
}

// =====================================================
// MICROSD INITIALIZATION
// =====================================================

void initializeSD() {

  sdSPI.begin(
    SD_SCK,
    SD_MISO,
    SD_MOSI,
    SD_CS
  );

  sd_OK = SD.begin(SD_CS, sdSPI);

  if (!sd_OK) {

    Serial.println("[ERROR] microSD initialization failed");
    return;
  }

  Serial.println("[OK] microSD initialized");

  if (!SD.exists("/monitor.csv")) {

    dataFile = SD.open(
      "/monitor.csv",
      FILE_WRITE
    );

    if (dataFile) {

      dataFile.println(
        "Time_ms,"
        "RPM,"
        "GeneratorVoltage_V,"
        "GeneratorCurrent_mA,"
        "GeneratorPower_W,"
        "BatteryTemp_C,"
        "BottomTemp_C,"
        "TopTemp_C,"
        "TemperatureDifference_C,"
        "BatteryVoltage_V,"
        "BatteryCurrent_mA,"
        "BatteryPower_W,"
        "BatteryStatus,"
        "PhoneVoltage_V,"
        "PhoneCurrent_mA,"
        "PhonePower_W,"
        "Pressure_Pa,"
        "SDPTemperature_C"
      );

      dataFile.close();
    }
  }
}

// =====================================================
// MICROSD DATA LOGGING
// =====================================================

void logToSD() {

  if (!sd_OK) return;

  dataFile = SD.open(
    "/monitor.csv",
    FILE_APPEND
  );

  if (!dataFile) {

    Serial.println("[SD ERROR] Unable to open monitor.csv");
    return;
  }

  dataFile.print(latestTime);
  dataFile.print(",");

  dataFile.print(rpm);
  dataFile.print(",");

  dataFile.print(generatorVoltage);
  dataFile.print(",");

  dataFile.print(generatorCurrent);
  dataFile.print(",");

  dataFile.print(generatorPower);
  dataFile.print(",");

  dataFile.print(batteryTemp);
  dataFile.print(",");

  dataFile.print(bottomTemp);
  dataFile.print(",");

  dataFile.print(topTemp);
  dataFile.print(",");

  dataFile.print(temperatureDifference);
  dataFile.print(",");

  dataFile.print(batteryVoltage);
  dataFile.print(",");

  dataFile.print(batteryCurrent);
  dataFile.print(",");

  dataFile.print(batteryPower);
  dataFile.print(",");

  dataFile.print(batteryStatus);
  dataFile.print(",");

  dataFile.print(phoneVoltage);
  dataFile.print(",");

  dataFile.print(phoneCurrent);
  dataFile.print(",");

  dataFile.print(phonePower);
  dataFile.print(",");

  dataFile.print(pressure);
  dataFile.print(",");

  dataFile.println(pressureTemperature);

  dataFile.close();

  Serial.println("[SD] Data saved");
}

// =====================================================
// SENSOR READING
// =====================================================

void readSensors() {

  unsigned long currentTime = millis();

  unsigned long elapsedTime =
    currentTime - previousSampleTime;

  if (elapsedTime == 0) {
    elapsedTime = SAMPLE_INTERVAL;
  }

  previousSampleTime = currentTime;

  latestTime = currentTime;

  // -----------------------------------------
  // 1. TURBINE RPM
  // -----------------------------------------

  noInterrupts();

  unsigned long pulses = hallPulses;
  hallPulses = 0;

  interrupts();

  rpm =
    (pulses * 60000.0) /
    (PULSES_PER_REV * elapsedTime);

  // -----------------------------------------
  // 2. TEMPERATURE SENSORS
  // -----------------------------------------

  tempSensors.requestTemperatures();

  batteryTemp =
    tempSensors.getTempCByIndex(0);

  bottomTemp =
    tempSensors.getTempCByIndex(1);

  topTemp =
    tempSensors.getTempCByIndex(2);

  if (batteryTemp == DEVICE_DISCONNECTED_C) {
    batteryTemp = NAN;
  }

  if (bottomTemp == DEVICE_DISCONNECTED_C) {
    bottomTemp = NAN;
  }

  if (topTemp == DEVICE_DISCONNECTED_C) {
    topTemp = NAN;
  }

  temperatureDifference = NAN;

  if (
    isfinite(topTemp) &&
    isfinite(bottomTemp)
  ) {

    temperatureDifference =
      topTemp - bottomTemp;
  }

  // -----------------------------------------
  // 3. GENERATOR INA219
  // -----------------------------------------

  generatorVoltage = NAN;
  generatorCurrent = NAN;
  generatorPower = NAN;

  if (generatorINA_OK) {

    generatorVoltage =
      generatorINA.getBusVoltage_V() +
      generatorINA.getShuntVoltage_mV() / 1000.0;

    generatorCurrent =
      generatorINA.getCurrent_mA();

    generatorPower =
      generatorVoltage *
      generatorCurrent / 1000.0;
  }

  // -----------------------------------------
  // 4. BATTERY INA219
  // -----------------------------------------

  batteryVoltage = NAN;
  batteryCurrent = NAN;
  batteryPower = NAN;

  batteryStatus = "ERROR";

  if (batteryINA_OK) {

    batteryVoltage =
      batteryINA.getBusVoltage_V() +
      batteryINA.getShuntVoltage_mV() / 1000.0;

    batteryCurrent =
      batteryINA.getCurrent_mA();

    batteryPower =
      batteryVoltage *
      batteryCurrent / 1000.0;

    // Positive = DISCHARGING
    // Negative = CHARGING

    if (batteryCurrent > 5.0) {

      batteryStatus = "DISCHARGING";

    } else if (batteryCurrent < -5.0) {

      batteryStatus = "CHARGING";

    } else {

      batteryStatus = "IDLE";
    }
  }

  // -----------------------------------------
  // 5. PHONE OUTPUT INA219
  // -----------------------------------------

  phoneVoltage = NAN;
  phoneCurrent = NAN;
  phonePower = NAN;

  if (phoneINA_OK) {

    phoneVoltage =
      phoneINA.getBusVoltage_V() +
      phoneINA.getShuntVoltage_mV() / 1000.0;

    phoneCurrent =
      phoneINA.getCurrent_mA();

    phonePower =
      phoneVoltage *
      phoneCurrent / 1000.0;
  }

  // -----------------------------------------
  // 6. SDP810 PRESSURE SENSOR
  // -----------------------------------------

  pressure = NAN;
  pressureTemperature = NAN;

  if (pressure_OK) {

    uint16_t error = sdp.readMeasurement(
      pressure,
      pressureTemperature
    );

    if (error != 0) {

      pressure = NAN;
      pressureTemperature = NAN;
    }
  }

  // -----------------------------------------
  // 7. SERIAL MONITOR
  // -----------------------------------------

  Serial.println("\n====================================");
  Serial.println("       ADPG LIVE MONITOR");
  Serial.println("====================================");

  Serial.print("RPM: ");
  Serial.println(rpm);

  Serial.print("Generator Voltage: ");
  Serial.println(generatorVoltage);

  Serial.print("Generator Current: ");
  Serial.println(generatorCurrent);

  Serial.print("Generator Power: ");
  Serial.println(generatorPower);

  Serial.print("Battery Temperature: ");
  Serial.println(batteryTemp);

  Serial.print("Bottom Temperature: ");
  Serial.println(bottomTemp);

  Serial.print("Top Temperature: ");
  Serial.println(topTemp);

  Serial.print("Temperature Difference: ");
  Serial.println(temperatureDifference);

  Serial.print("Battery Voltage: ");
  Serial.println(batteryVoltage);

  Serial.print("Battery Current: ");
  Serial.println(batteryCurrent);

  Serial.print("Battery Power: ");
  Serial.println(batteryPower);

  Serial.print("Battery Status: ");
  Serial.println(batteryStatus);

  Serial.print("Phone Voltage: ");
  Serial.println(phoneVoltage);

  Serial.print("Phone Current: ");
  Serial.println(phoneCurrent);

  Serial.print("Phone Power: ");
  Serial.println(phonePower);

  Serial.print("Differential Pressure: ");
  Serial.println(pressure);

  Serial.print("SDP810 Temperature: ");
  Serial.println(pressureTemperature);

  // -----------------------------------------
  // 8. SD LOGGING
  // -----------------------------------------

  logToSD();

  Serial.println("====================================");
}

// =====================================================
// SETUP
// =====================================================

void setup() {

  Serial.begin(115200);

  delay(1000);

  Serial.println("\n====================================");
  Serial.println(" ADPG MONITORING SYSTEM");
  Serial.println(" ESP32 INITIALIZATION");
  Serial.println("====================================");

  // -----------------------------------------
  // I2C
  // -----------------------------------------

  Wire.begin(
    SDA_PIN,
    SCL_PIN
  );

  Wire.setClock(100000);

  // -----------------------------------------
  // GENERATOR INA219
  // -----------------------------------------

  generatorINA_OK =
    generatorINA.begin(&Wire);

  if (generatorINA_OK) {

    generatorINA.setCalibration_32V_2A();

    Serial.println("[OK] Generator INA219 0x40");

  } else {

    Serial.println("[ERROR] Generator INA219");
  }

  // -----------------------------------------
  // BATTERY INA219
  // -----------------------------------------

  batteryINA_OK =
    batteryINA.begin(&Wire);

  if (batteryINA_OK) {

    batteryINA.setCalibration_32V_2A();

    Serial.println("[OK] Battery INA219 0x41");

  } else {

    Serial.println("[ERROR] Battery INA219");
  }

  // -----------------------------------------
  // PHONE INA219
  // -----------------------------------------

  phoneINA_OK =
    phoneINA.begin(&Wire);

  if (phoneINA_OK) {

    phoneINA.setCalibration_32V_2A();

    Serial.println("[OK] Phone INA219 0x44");

  } else {

    Serial.println("[ERROR] Phone INA219");
  }

  // -----------------------------------------
  // DS18B20
  // -----------------------------------------

  tempSensors.begin();

  tempSensors.setResolution(12);

  Serial.print("DS18B20 detected: ");

  Serial.println(
    tempSensors.getDeviceCount()
  );

  printTemperatureAddresses();

  // -----------------------------------------
  // HALL SENSOR
  // -----------------------------------------

  pinMode(
    HALL_PIN,
    INPUT_PULLUP
  );

  attachInterrupt(
    digitalPinToInterrupt(HALL_PIN),
    countHallPulse,
    FALLING
  );

  Serial.println("[OK] Hall sensor configured");

  // -----------------------------------------
  // SDP810
  // -----------------------------------------

  sdp.begin(
    Wire,
    SDP8XX_I2C_ADDRESS_0
  );

  uint16_t sdpError =
    sdp.stopContinuousMeasurement();

  delay(10);

  sdpError =
    sdp.startContinuousMeasurementWithDiffPressureTCompAndAveraging();

  pressure_OK = (sdpError == 0);

  if (pressure_OK) {

    Serial.println("[OK] SDP810 initialized");

  } else {

    Serial.print("[ERROR] SDP810: ");
    Serial.println(sdpError);
  }

  // -----------------------------------------
  // MICROSD
  // -----------------------------------------

  initializeSD();

  // -----------------------------------------
  // WIFI
  // -----------------------------------------

  connectWiFi();

  // -----------------------------------------
  // WEB SERVER ROUTES
  // -----------------------------------------

  server.on(
    "/",
    HTTP_GET,
    handleRoot
  );

  server.on(
    "/api/data",
    HTTP_GET,
    handleSensorData
  );

  server.on(
    "/api/latest",
    HTTP_GET,
    handleSensorData
  );

  server.onNotFound(
    handleNotFound
  );

  server.begin();

  Serial.println("\n[OK] ESP32 HTTP server started");

  Serial.print("Open this URL: http://");
  Serial.print(WiFi.localIP());
  Serial.println("/api/data");

  Serial.println("\nINITIALIZATION COMPLETE");
  Serial.println("====================================");

  previousSampleTime = millis();
}

// =====================================================
// MAIN LOOP
// =====================================================

void loop() {

  // Process incoming browser/Python requests
  server.handleClient();

  // Reconnect Wi-Fi if disconnected
  if (WiFi.status() != WL_CONNECTED) {

    WiFi.reconnect();

    delay(10);
  }

  // Read sensors every second
  if (
    millis() - previousSampleTime >= SAMPLE_INTERVAL
  ) {

    readSensors();
  }

  // Handle requests again after sensor readings
  server.handleClient();
}

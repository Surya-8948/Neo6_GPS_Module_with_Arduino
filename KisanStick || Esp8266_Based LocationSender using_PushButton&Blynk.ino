
// Blynk template definitions must appear before Blynk headers.

#define BLYNK_TEMPLATE_ID "****************************"
#define BLYNK_TEMPLATE_NAME "kisan Stick"
#define BLYNK_AUTH_TOKEN "***************************************"

#define BLYNK_NO_FANCY_LOGO

#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <SoftwareSerial.h>
#include <TinyGPS++.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

// -----------------------------------------------------------------------------
// User configuration
// -----------------------------------------------------------------------------
static const char WIFI_SSID[]     = "***********************";
static const char WIFI_PASSWORD[] = "***********************";

// -----------------------------------------------------------------------------
// Hardware pin mapping (NodeMCU board labels)
// -----------------------------------------------------------------------------
static const uint8_t GPS_RX_PIN          = D1;  // ESP RX <- GPS TX (GPIO5)
static const uint8_t GPS_TX_PIN          = D2;  // ESP TX -> GPS RX (GPIO4)
static const uint32_t GPS_BAUD           = 9600UL;

static const uint8_t RELAY1_PIN          = D5;  // GPIO14
static const uint8_t RELAY2_PIN          = D6;  // GPIO12
static const bool    RELAY_ACTIVE_LOW    = true;

static const uint8_t SOS_BUTTON_PIN      = D7;  // GPIO13, button to GND
static const uint8_t RELAY1_BUTTON_PIN   = D8;  // GPIO15, external pulldown, button to 3V3
static const uint8_t RELAY2_BUTTON_PIN   = D0;  // GPIO16, external pull-up, button to GND

// Blynk Event code created in the template's Events & Notifications section.
static const char SOS_EVENT_CODE[] = "sos_location";

// -----------------------------------------------------------------------------
// Timing / bounded-work configuration
// -----------------------------------------------------------------------------
static const uint8_t  GPS_BYTES_PER_LOOP       = 128U;
static const uint16_t BUTTON_DEBOUNCE_MS       = 35U;
static const uint32_t GPS_FIX_STALE_MS         = 5000UL;
static const uint32_t GPS_FIELD_STALE_MS       = 5000UL;
static const uint32_t WIFI_RETRY_INITIAL_MS    = 15000UL;
static const uint32_t WIFI_RETRY_AFTER_LOSS_MS = 5000UL;
static const uint32_t WIFI_RETRY_MAX_MS        = 60000UL;
static const uint32_t SOS_EVENT_MIN_GAP_MS     = 1100UL;
static const uint8_t  SOS_QUEUE_CAPACITY       = 4U;
static const size_t   SOS_DESCRIPTION_SIZE     = 112U;

// -----------------------------------------------------------------------------
// Devices / application state
// -----------------------------------------------------------------------------
TinyGPSPlus gps;
SoftwareSerial gpsSerial(GPS_RX_PIN, GPS_TX_PIN);
BlynkTimer timer;

struct GpsSnapshot {
  bool hasPosition;
  double latitude;
  double longitude;
  uint32_t positionUpdatedAt;

  bool hasAltitude;
  double altitudeMeters;
  uint32_t altitudeUpdatedAt;

  bool hasSpeed;
  double speedKmph;
  uint32_t speedUpdatedAt;

  bool hasSatellites;
  uint32_t satellites;
  uint32_t satellitesUpdatedAt;

  bool hasHdop;
  double hdop;
  uint32_t hdopUpdatedAt;

  bool hasUtcTime;
  uint16_t utcYear;
  uint8_t utcMonth;
  uint8_t utcDay;
  uint8_t utcHour;
  uint8_t utcMinute;
  uint8_t utcSecond;
  uint32_t utcUpdatedAt;
};

struct DebouncedButton {
  uint8_t pin;
  uint8_t activeLevel;
  uint16_t debounceMs;
  bool rawPressed;
  bool stablePressed;
  bool pressedEvent;
  uint32_t rawChangedAt;
};

GpsSnapshot gpsSnapshot = {};

DebouncedButton sosButton = {
  SOS_BUTTON_PIN, LOW, BUTTON_DEBOUNCE_MS, false, false, false, 0UL
};
DebouncedButton relay1Button = {
  RELAY1_BUTTON_PIN, HIGH, BUTTON_DEBOUNCE_MS, false, false, false, 0UL
};
DebouncedButton relay2Button = {
  RELAY2_BUTTON_PIN, LOW, BUTTON_DEBOUNCE_MS, false, false, false, 0UL
};

bool relay1On = false;
bool relay2On = false;
bool appSosArmed = true;

char sosEventQueue[SOS_QUEUE_CAPACITY][SOS_DESCRIPTION_SIZE];
uint8_t sosQueueHead = 0U;
uint8_t sosQueueTail = 0U;
uint8_t sosQueueCount = 0U;
uint32_t sosQueueOverflowCount = 0UL;
uint32_t lastSosEventDispatchAt = 0UL;
bool sosEventDispatchStarted = false;

uint32_t lastWifiBeginAt = 0UL;
uint32_t wifiRetryIntervalMs = WIFI_RETRY_INITIAL_MS;
bool wifiWasConnected = false;

// -----------------------------------------------------------------------------
// Function declarations
// -----------------------------------------------------------------------------
void readGPS();
void updateGPS();
bool gpsFixAvailable();
bool timestampIsFresh(bool hasValue, uint32_t updatedAt, uint32_t maxAgeMs);

void initializeButton(DebouncedButton &button);
void updateButton(DebouncedButton &button, uint32_t now);
void handleSOSButton();
void handleRelay1Button();
void handleRelay2Button();
void updateRelay1();
void updateRelay2();
uint8_t relayOutputLevel(bool isOn);

void sendGoogleMapsLink();
bool formatCoordinate(double coordinate, char *output, size_t outputSize);
bool enqueueSosEvent(const char *description);
void serviceSosEventQueue();

void printGPS();
void printISTTime();
void printSystemStatus();
void printDateTime(uint16_t year, uint8_t month, uint8_t day,
                   uint8_t hour, uint8_t minute, uint8_t second);
uint8_t daysInMonth(uint16_t year, uint8_t month);
bool isLeapYear(uint16_t year);

void wifiReconnect();
void blynkReconnect();

// -----------------------------------------------------------------------------
// Blynk callbacks
// -----------------------------------------------------------------------------
BLYNK_CONNECTED() {
  // Do not sync V0: replaying a stored SOS command on reconnect could duplicate
  // an event. The app button should be configured as a momentary PUSH button.
  appSosArmed = true;
  Serial.println(F("Blynk connected; no virtual-pin state sync required."));
}

BLYNK_WRITE(V0) {
  const int requested = param.asInt();

  if (requested == 0) {
    appSosArmed = true;
    return;
  }

  // Rising-edge behavior: repeated HIGH writes/long presses do not repeat SOS.
  if (appSosArmed) {
    appSosArmed = false;
    Serial.println(F("Blynk V0 SOS press."));
    sendGoogleMapsLink();
  }
}

// -----------------------------------------------------------------------------
// Arduino entry points
// -----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println(F("GPS SOS Tracker starting..."));

  // Set relay output latches to the OFF level before enabling output mode.
  digitalWrite(RELAY1_PIN, relayOutputLevel(false));
  digitalWrite(RELAY2_PIN, relayOutputLevel(false));
  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  updateRelay1();
  updateRelay2();

  pinMode(SOS_BUTTON_PIN, INPUT_PULLUP);
  // D8/GPIO15 must stay LOW during boot: external 10k pulldown required.
  pinMode(RELAY1_BUTTON_PIN, INPUT);
  // GPIO16/D0 input uses an external pull-up; it is polled (no interrupt needed).
  pinMode(RELAY2_BUTTON_PIN, INPUT);

  initializeButton(sosButton);
  initializeButton(relay1Button);
  initializeButton(relay2Button);

  gpsSerial.begin(GPS_BAUD);

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);  // Starts an asynchronous association.
  lastWifiBeginAt = millis();
  wifiRetryIntervalMs = WIFI_RETRY_INITIAL_MS;
  Serial.println(F("Wi-Fi association started."));

  // config() does not wait for a connection. Blynk.run() below services the
  // protocol state machine and its reconnect attempts; do not call Blynk.begin()
  // or Blynk.connect() in this sketch.
  Blynk.config(BLYNK_AUTH_TOKEN);

  timer.setInterval(250L, serviceSosEventQueue);
  timer.setInterval(1000L, printSystemStatus);
}

void loop() {
  // Keep GPS parsing and local controls serviced on every pass.
  readGPS();
  updateGPS();
  handleSOSButton();
  handleRelay1Button();
  handleRelay2Button();

  wifiReconnect();
  blynkReconnect();
  timer.run();

  // Yield to ESP8266 Wi-Fi/TCP background work without using delay().
  yield();
}

// -----------------------------------------------------------------------------
// GPS service
// -----------------------------------------------------------------------------
void readGPS() {
  // A bounded for-loop drains available bytes; it never waits for a byte.
  for (uint8_t i = 0U; i < GPS_BYTES_PER_LOOP; ++i) {
    if (gpsSerial.available() <= 0) {
      break;
    }

    const int incoming = gpsSerial.read();
    if (incoming >= 0) {
      gps.encode(static_cast<char>(incoming));
    }
  }
}

void updateGPS() {
  const uint32_t now = millis();

  // Keep the last valid position even after the receiver loses its fix.
  if (gps.location.isUpdated() && gps.location.isValid()) {
    const double latitude = gps.location.lat();
    const double longitude = gps.location.lng();

    if (!isnan(latitude) && !isinf(latitude) &&
        !isnan(longitude) && !isinf(longitude) &&
        latitude >= -90.0 && latitude <= 90.0 &&
        longitude >= -180.0 && longitude <= 180.0) {
      gpsSnapshot.latitude = latitude;
      gpsSnapshot.longitude = longitude;
      gpsSnapshot.positionUpdatedAt = now;
      gpsSnapshot.hasPosition = true;
    }
  }

  if (gps.altitude.isUpdated() && gps.altitude.isValid()) {
    gpsSnapshot.altitudeMeters = gps.altitude.meters();
    gpsSnapshot.altitudeUpdatedAt = now;
    gpsSnapshot.hasAltitude = true;
  }

  if (gps.speed.isUpdated() && gps.speed.isValid()) {
    gpsSnapshot.speedKmph = gps.speed.kmph();
    gpsSnapshot.speedUpdatedAt = now;
    gpsSnapshot.hasSpeed = true;
  }

  if (gps.satellites.isUpdated() && gps.satellites.isValid()) {
    gpsSnapshot.satellites = gps.satellites.value();
    gpsSnapshot.satellitesUpdatedAt = now;
    gpsSnapshot.hasSatellites = true;
  }

  if (gps.hdop.isUpdated() && gps.hdop.isValid()) {
    gpsSnapshot.hdop = gps.hdop.hdop();
    gpsSnapshot.hdopUpdatedAt = now;
    gpsSnapshot.hasHdop = true;
  }

  // UTC date/time is useful even when location is temporarily unavailable.
  const bool freshDate = gps.date.isValid() &&
                         gps.date.age() <= GPS_FIELD_STALE_MS;
  const bool freshTime = gps.time.isValid() &&
                         gps.time.age() <= GPS_FIELD_STALE_MS;
  if (freshDate && freshTime) {
    gpsSnapshot.utcYear = gps.date.year();
    gpsSnapshot.utcMonth = gps.date.month();
    gpsSnapshot.utcDay = gps.date.day();
    gpsSnapshot.utcHour = gps.time.hour();
    gpsSnapshot.utcMinute = gps.time.minute();
    gpsSnapshot.utcSecond = gps.time.second();
    gpsSnapshot.utcUpdatedAt = now;
    gpsSnapshot.hasUtcTime = true;
  } else {
    gpsSnapshot.hasUtcTime = false;
  }
}

bool gpsFixAvailable() {
  if (!gpsSnapshot.hasPosition ||
      !timestampIsFresh(gpsSnapshot.hasPosition,
                        gpsSnapshot.positionUpdatedAt,
                        GPS_FIX_STALE_MS)) {
    return false;
  }

  // TinyGPS++ invalidates position on a valid no-fix sentence; age also handles
  // a receiver that stops transmitting entirely.
  return gps.location.isValid() && gps.location.age() <= GPS_FIX_STALE_MS;
}

bool timestampIsFresh(bool hasValue, uint32_t updatedAt, uint32_t maxAgeMs) {
  return hasValue && static_cast<uint32_t>(millis() - updatedAt) <= maxAgeMs;
}

// -----------------------------------------------------------------------------
// Button debounce / local relay control
// -----------------------------------------------------------------------------
void initializeButton(DebouncedButton &button) {
  const bool pressed = (digitalRead(button.pin) == button.activeLevel);
  button.rawPressed = pressed;
  button.stablePressed = pressed;
  button.pressedEvent = false;
  button.rawChangedAt = millis();
}

void updateButton(DebouncedButton &button, uint32_t now) {
  button.pressedEvent = false;
  const bool rawPressed = (digitalRead(button.pin) == button.activeLevel);

  if (rawPressed != button.rawPressed) {
    button.rawPressed = rawPressed;
    button.rawChangedAt = now;
  }

  if (button.rawPressed != button.stablePressed &&
      static_cast<uint32_t>(now - button.rawChangedAt) >= button.debounceMs) {
    button.stablePressed = button.rawPressed;
    if (button.stablePressed) {
      // Exactly one event on the stable press edge; release only rearms it.
      button.pressedEvent = true;
    }
  }
}

void handleSOSButton() {
  updateButton(sosButton, millis());
  if (sosButton.pressedEvent) {
    Serial.println(F("Hardware SOS button press."));
    sendGoogleMapsLink();
  }
}

void handleRelay1Button() {
  updateButton(relay1Button, millis());
  if (relay1Button.pressedEvent) {
    relay1On = !relay1On;
    updateRelay1();
    Serial.print(F("Relay 1: "));
    Serial.println(relay1On ? F("ON") : F("OFF"));
  }
}

void handleRelay2Button() 
{
  updateButton(relay2Button, millis());
  if (relay2Button.pressedEvent) 
  {
    relay2On = !relay2On;
    updateRelay2();
    Serial.print(F("Relay 2: "));
    Serial.println(relay2On ? F("ON") : F("OFF"));
  }
}

uint8_t relayOutputLevel(bool isOn) {
  if (RELAY_ACTIVE_LOW) {
    return isOn ? LOW : HIGH;
  }
  return isOn ? HIGH : LOW;
}

void updateRelay1() {
  digitalWrite(RELAY1_PIN, relayOutputLevel(relay1On));
}

void updateRelay2() {
  digitalWrite(RELAY2_PIN, relayOutputLevel(relay2On));
}

// -----------------------------------------------------------------------------
// SOS URL + Blynk event queue
// -----------------------------------------------------------------------------
void sendGoogleMapsLink() {
  char description[SOS_DESCRIPTION_SIZE];

  if (gpsFixAvailable()) {
    char latitudeText[24];
    char longitudeText[24];

    if (formatCoordinate(gpsSnapshot.latitude, latitudeText, sizeof(latitudeText)) &&formatCoordinate(gpsSnapshot.longitude, longitudeText, sizeof(longitudeText)))
     {
      const int written = snprintf(description, sizeof(description),"Emergency!\nI am in trouble.\nLocation:\nhttps://maps.google.com/?q=%s,%s",latitudeText, longitudeText);
      if (written < 0 || static_cast<size_t>(written) >= sizeof(description)) {
        snprintf(description, sizeof(description), "%s",
                 "GPS Fix Not Available");
      }
    } else {
      snprintf(description, sizeof(description), "%s",
               "GPS Fix Not Available");
    }
  } else {
    snprintf(description, sizeof(description), "%s", "GPS Fix Not Available");
  }

  Serial.print(F("SOS event description: "));
  Serial.println(description);

  if (enqueueSosEvent(description)) {
    Serial.print(F("SOS event queued; pending count: "));
    Serial.println(sosQueueCount);
  }
}

bool formatCoordinate(double coordinate, char *output, size_t outputSize) {
  if (output == NULL || outputSize == 0U ||
      isnan(coordinate) || isinf(coordinate)) {
    return false;
  }

  // Integer microdegrees avoid relying on printf floating-point support.
  const long scaled = static_cast<long>(lround(coordinate * 1000000.0));
  const bool negative = (scaled < 0L);
  const unsigned long magnitude = static_cast<unsigned long>(
      negative ? -scaled : scaled);
  const unsigned long whole = magnitude / 1000000UL;
  const unsigned long fraction = magnitude % 1000000UL;

  const int written = snprintf(output, outputSize, "%s%lu.%06lu",
                               negative ? "-" : "",
                               whole, fraction);
  return written > 0 && static_cast<size_t>(written) < outputSize;
}

bool enqueueSosEvent(const char *description) {
  if (description == NULL) {
    return false;
  }

  if (sosQueueCount >= SOS_QUEUE_CAPACITY) {
    ++sosQueueOverflowCount;
    Serial.print(F("SOS queue full; event dropped. Overflow count: "));
    Serial.println(sosQueueOverflowCount);
    return false;
  }

  snprintf(sosEventQueue[sosQueueTail], SOS_DESCRIPTION_SIZE, "%s", description);
  sosQueueTail = static_cast<uint8_t>((sosQueueTail + 1U) % SOS_QUEUE_CAPACITY);
  ++sosQueueCount;
  return true;
}

void serviceSosEventQueue() {
  if (sosQueueCount == 0U || !Blynk.connected()) {
    return;
  }

  const uint32_t now = millis();
  if (sosEventDispatchStarted &&
      static_cast<uint32_t>(now - lastSosEventDispatchAt) < SOS_EVENT_MIN_GAP_MS) {
    return;
  }

  // Blynk event sends are best-effort: the library API provides no delivery ACK.
  // The RAM queue is popped after submission to prevent duplicate notifications.
  Blynk.logEvent(SOS_EVENT_CODE, sosEventQueue[sosQueueHead]);
  lastSosEventDispatchAt = millis();
  sosEventDispatchStarted = true;
  Serial.print(F("Submitted Blynk event: "));
  Serial.println(sosEventQueue[sosQueueHead]);

  sosQueueHead = static_cast<uint8_t>((sosQueueHead + 1U) % SOS_QUEUE_CAPACITY);
  --sosQueueCount;
}

// -----------------------------------------------------------------------------
// Serial diagnostics
// -----------------------------------------------------------------------------
void printGPS() {
  const bool fix = gpsFixAvailable();
  Serial.print(F("GPS Fix: "));
  Serial.println(fix ? F("YES (fresh)") : F("NO / stale"));

  Serial.print(F("Latitude: "));
  if (gpsSnapshot.hasPosition) {
    Serial.print(gpsSnapshot.latitude, 6);
    if (!fix) {
      Serial.print(F(" (last valid; not a current fix)"));
    }
    Serial.println();
  } else {
    Serial.println(F("N/A"));
  }

  Serial.print(F("Longitude: "));
  if (gpsSnapshot.hasPosition) {
    Serial.print(gpsSnapshot.longitude, 6);
    if (!fix) {
      Serial.print(F(" (last valid; not a current fix)"));
    }
    Serial.println();
  } else {
    Serial.println(F("N/A"));
  }

  Serial.print(F("Altitude: "));
  if (timestampIsFresh(gpsSnapshot.hasAltitude, gpsSnapshot.altitudeUpdatedAt,
                       GPS_FIELD_STALE_MS)) {
    Serial.print(gpsSnapshot.altitudeMeters, 2);
    Serial.println(F(" m"));
  } else {
    Serial.println(F("N/A"));
  }

  Serial.print(F("Speed: "));
  if (timestampIsFresh(gpsSnapshot.hasSpeed, gpsSnapshot.speedUpdatedAt,
                       GPS_FIELD_STALE_MS)) {
    Serial.print(gpsSnapshot.speedKmph, 2);
    Serial.println(F(" km/h"));
  } else {
    Serial.println(F("N/A"));
  }

  Serial.print(F("Satellites: "));
  if (timestampIsFresh(gpsSnapshot.hasSatellites,
                       gpsSnapshot.satellitesUpdatedAt,
                       GPS_FIELD_STALE_MS)) {
    Serial.println(gpsSnapshot.satellites);
  } else {
    Serial.println(F("N/A"));
  }

  Serial.print(F("HDOP: "));
  if (timestampIsFresh(gpsSnapshot.hasHdop, gpsSnapshot.hdopUpdatedAt,
                       GPS_FIELD_STALE_MS)) {
    Serial.println(gpsSnapshot.hdop, 2);
  } else {
    Serial.println(F("N/A"));
  }

  Serial.print(F("UTC Time: "));
  if (gpsSnapshot.hasUtcTime &&
      timestampIsFresh(gpsSnapshot.hasUtcTime, gpsSnapshot.utcUpdatedAt,
                       GPS_FIELD_STALE_MS)) {
    printDateTime(gpsSnapshot.utcYear, gpsSnapshot.utcMonth,
                  gpsSnapshot.utcDay, gpsSnapshot.utcHour,
                  gpsSnapshot.utcMinute, gpsSnapshot.utcSecond);
    Serial.println();
  } else {
    Serial.println(F("N/A"));
  }
}

void printISTTime() {
  Serial.print(F("IST Time (UTC+05:30): "));
  if (!gpsSnapshot.hasUtcTime ||
      !timestampIsFresh(gpsSnapshot.hasUtcTime, gpsSnapshot.utcUpdatedAt,
                        GPS_FIELD_STALE_MS)) {
    Serial.println(F("N/A"));
    return;
  }

  uint16_t year = gpsSnapshot.utcYear;
  uint8_t month = gpsSnapshot.utcMonth;
  uint8_t day = gpsSnapshot.utcDay;
  uint8_t hour = gpsSnapshot.utcHour;
  uint8_t minute = static_cast<uint8_t>(gpsSnapshot.utcMinute + 30U);
  const uint8_t second = gpsSnapshot.utcSecond;

  if (minute >= 60U) {
    minute = static_cast<uint8_t>(minute - 60U);
    ++hour;
  }
  hour = static_cast<uint8_t>(hour + 5U);
  if (hour >= 24U) {
    hour = static_cast<uint8_t>(hour - 24U);
    ++day;
    if (day > daysInMonth(year, month)) {
      day = 1U;
      ++month;
      if (month > 12U) {
        month = 1U;
        ++year;
      }
    }
  }

  printDateTime(year, month, day, hour, minute, second);
  Serial.println();
}

void printSystemStatus() {
  Serial.println();
  Serial.println(F("========== SYSTEM STATUS =========="));
  printGPS();
  printISTTime();

  Serial.print(F("WiFi Status: "));
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("CONNECTED, IP="));
    Serial.print(WiFi.localIP());
    Serial.print(F(", RSSI="));
    Serial.print(WiFi.RSSI());
    Serial.println(F(" dBm"));
  } else {
    Serial.print(F("DISCONNECTED, status="));
    Serial.println(static_cast<int>(WiFi.status()));
  }

  Serial.print(F("Blynk Status: "));
  Serial.println(Blynk.connected() ? F("CONNECTED") : F("DISCONNECTED"));

  Serial.print(F("Relay 1 Status: "));
  Serial.println(relay1On ? F("ON") : F("OFF"));
  Serial.print(F("Relay 2 Status: "));
  Serial.println(relay2On ? F("ON") : F("OFF"));

  Serial.print(F("SOS queue depth: "));
  Serial.print(sosQueueCount);
  Serial.print(F(" / "));
  Serial.println(SOS_QUEUE_CAPACITY);
  Serial.println(F("==================================="));
}

void printDateTime(uint16_t year, uint8_t month, uint8_t day,
                   uint8_t hour, uint8_t minute, uint8_t second) {
  if (year < 1000U) Serial.print('0');
  if (year < 100U) Serial.print('0');
  if (year < 10U) Serial.print('0');
  Serial.print(year);
  Serial.print('-');

  if (month < 10U) Serial.print('0');
  Serial.print(month);
  Serial.print('-');

  if (day < 10U) Serial.print('0');
  Serial.print(day);
  Serial.print(' ');

  if (hour < 10U) Serial.print('0');
  Serial.print(hour);
  Serial.print(':');

  if (minute < 10U) Serial.print('0');
  Serial.print(minute);
  Serial.print(':');

  if (second < 10U) Serial.print('0');
  Serial.print(second);
}

uint8_t daysInMonth(uint16_t year, uint8_t month)
 {
  static const uint8_t monthLengths[12] = {
    31U, 28U, 31U, 30U, 31U, 30U,
    31U, 31U, 30U, 31U, 30U, 31U
  };

  if (month < 1U || month > 12U)
   {
    return 31U;
  }
  if (month == 2U && isLeapYear(year))
   {
    return 29U;
  }
  return monthLengths[month - 1U];
}

bool isLeapYear(uint16_t year)
 {
  return ((year % 4U) == 0U && (year % 100U) != 0U) ||
         ((year % 400U) == 0U);
}

// -----------------------------------------------------------------------------
// Wi-Fi / Blynk connection service
// -----------------------------------------------------------------------------
void wifiReconnect() 
{
  const uint32_t now = millis();
  const bool connected = (WiFi.status() == WL_CONNECTED);

  if (connected)
   {
    if (!wifiWasConnected) 
    {
      Serial.print(F("Wi-Fi connected. IP address: "));
      Serial.println(WiFi.localIP());
    }
    wifiWasConnected = true;
    wifiRetryIntervalMs = WIFI_RETRY_INITIAL_MS;
    return;
  }

  if (wifiWasConnected) {
    Serial.println(F("Wi-Fi connection lost; reconnecting automatically."));
    wifiWasConnected = false;
    wifiRetryIntervalMs = WIFI_RETRY_AFTER_LOSS_MS;
    lastWifiBeginAt = now;
  }

  if (static_cast<uint32_t>(now - lastWifiBeginAt) >= wifiRetryIntervalMs)
   {
    Serial.println(F("Wi-Fi retry started (asynchronous)."));
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    lastWifiBeginAt = now;

    if (wifiRetryIntervalMs < WIFI_RETRY_MAX_MS) 
    {
      wifiRetryIntervalMs *= 2UL;
      if (wifiRetryIntervalMs > WIFI_RETRY_MAX_MS)
       {
        wifiRetryIntervalMs = WIFI_RETRY_MAX_MS;
      }
    }
  }
}

void blynkReconnect() 
{
  
  if (WiFi.status() == WL_CONNECTED || Blynk.connected()) 
  {
    Blynk.run();
  }
}

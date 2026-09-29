
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <TinyGPSPlus.h>
#include <ArduinoJson.h>

// ============================================================================
//  USER CONFIGURATION — edit these
// ============================================================================

// ---- WiFi ----
static const char* WIFI_SSID   = "Vimal";
static const char* WIFI_PASS   = "1234567890";
static const char* WIFI_HOST   = "gps-tracker";

// ---- MQTT (HiveMQ public broker) ----
// Dashboard uses wss://broker.hivemq.com:8884/mqtt
// ESP32 uses native MQTT/TLS on port 8883 (more reliable than WSS on MCU)
static const char* MQTT_HOST   = "";
static const int   MQTT_PORT   = 8883;
static const char* MQTT_USER   = "*******************";
static const char* MQTT_PASS   = "******************";
static const char* MQTT_TOPIC  = "gps/tracker01";   // must match dashboard

// ---- Device identity ----
static const char* DEVICE_ID  = "esp32-01";
static const char* DEVICE_NAME = "ESP32 Tracker";
static const char* FIRMWARE   = "1.0.0";

// ---- Pins ----
#define GPS_RX_PIN     16      // ESP32 RX2  <- GPS TX
#define GPS_TX_PIN     17      // ESP32 TX2  -> GPS RX
#define GPS_BAUD       9600
#define BAT_ADC_PIN    34      // battery divider on ADC1
#define BAT_DIVIDER    2.0f    // Vout = Vin / 2 with two 100k resistors
#define STATUS_LED     2

// ---- Timing / defaults ----
#define DEFAULT_PUBLISH_MS  5000
#define GPS_STALE_MS        10000
#define MQTT_RETRY_MS       5000
#define WIFI_RETRY_MS       10000
#define DEFAULT_MAX_KPH     80.0f
#define DEFAULT_LOW_BAT_V   3.5f
#define MAX_GEOFENCES       16
#define MAX_POLY_POINTS     32

// ============================================================================
//  GLOBALS
// ============================================================================

TinyGPSPlus      gps;
HardwareSerial   gpsSerial(2);
WiFiClientSecure net;
PubSubClient     mqtt(net);

struct RuntimeCfg {
  uint32_t publishMs  = DEFAULT_PUBLISH_MS;
  float    maxKph     = DEFAULT_MAX_KPH;
  float    lowBatV    = DEFAULT_LOW_BAT_V;
  String   deviceName = DEVICE_NAME;
} cfg;

enum class GeoType : uint8_t { Circle, Polygon, Route };

struct Geofence {
  bool     used      = false;
  uint8_t  id        = 0;
  GeoType  type      = GeoType::Circle;
  char     name[32]  = {0};
  char     color[10] = {0};
  bool     enabled   = true;
  float    radius    = 0;
  double   lat0 = 0, lng0 = 0;
  uint8_t  nPoints = 0;
  double   lat[MAX_POLY_POINTS] = {0};
  double   lng[MAX_POLY_POINTS] = {0};
  int8_t   wasInside = -1;   // -1 unknown, 0 outside, 1 inside
};

Geofence geofences[MAX_GEOFENCES];

uint32_t lastPublish  = 0;
uint32_t lastMqttTry  = 0;
uint32_t lastWifiTry  = 0;
bool     wasOnline    = false;
bool     lowBatLatch  = false;
bool     overSpdLatch = false;

// ============================================================================
//  GEO HELPERS — mirror the dashboard JS so behaviour is identical
// ============================================================================

static inline double toRad(double d) { return d * DEG_TO_RAD; }

double haversine(double lat1, double lng1, double lat2, double lng2) {
  double dLat = toRad(lat2 - lat1);
  double dLng = toRad(lng2 - lng1);
  double a = sin(dLat/2)*sin(dLat/2) +
             cos(toRad(lat1))*cos(toRad(lat2))*sin(dLng/2)*sin(dLng/2);
  return 2.0 * 6371000.0 * asin(sqrt(a));
}

double pointToSegment(double pLat, double pLng,
                      double aLat, double aLng,
                      double bLat, double bLng) {
  const double k = cos(toRad(pLat));
  double ax = (aLng - pLng) * k * 111320.0;
  double ay = (aLat - pLat) * 111320.0;
  double bx = (bLng - pLng) * k * 111320.0;
  double by = (bLat - pLat) * 111320.0;
  double dx = bx - ax, dy = by - ay;
  double len2 = dx*dx + dy*dy;
  double t = (len2 > 0) ? -(ax*dx + ay*dy) / len2 : 0;
  if (t < 0) t = 0; else if (t > 1) t = 1;
  double cx = ax + t*dx, cy = ay + t*dy;
  return sqrt(cx*cx + cy*cy);
}

bool pointInPolygon(double lat, double lng, const Geofence& g) {
  bool inside = false;
  for (uint8_t i = 0, j = g.nPoints - 1; i < g.nPoints; j = i++) {
    bool yi = g.lat[i] > lat, yj = g.lat[j] > lat;
    if (yi != yj) {
      double x = (g.lng[j] - g.lng[i]) * (lat - g.lat[i]) /
                 (g.lat[j] - g.lat[i]) + g.lng[i];
      if (lng < x) inside = !inside;
    }
  }
  return inside;
}

bool isInside(const Geofence& g, double lat, double lng) {
  switch (g.type) {
    case GeoType::Circle:
      return haversine(lat, lng, g.lat0, g.lng0) <= g.radius;
    case GeoType::Polygon:
      return pointInPolygon(lat, lng, g);
    case GeoType::Route:
      if (g.nPoints < 2) return false;
      for (uint8_t i = 1; i < g.nPoints; ++i) {
        if (pointToSegment(lat, lng,
                           g.lat[i-1], g.lng[i-1],
                           g.lat[i],   g.lng[i]) <= g.radius) return true;
      }
      return false;
  }
  return false;
}

// ============================================================================
//  FORWARD DECLARATIONS
// ============================================================================

void connectWiFi();
void connectMQTT();
void onMqttMessage(char* topic, byte* payload, unsigned int len);
void parseConfig(JsonObjectConst doc);
void publishTelemetry();
void publishEvent(const String& type, const String& msg, const char* level);
void checkGeofences(double lat, double lng);
float readBatteryVolts();

// ============================================================================
//  SETUP
// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println(F("=== ESP32 GPS Tracker ==="));
  Serial.printf("FW %s   Device %s\n", FIRMWARE, DEVICE_ID);

  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, LOW);

  analogReadResolution(12);
  analogSetPinAttenuation(BAT_ADC_PIN, ADC_11db);

  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  // Public HiveMQ test broker: CA not pinned for simplicity.
  // For production, replace with net.setCACert(ROOT_CA).
  net.setInsecure();
  net.setTimeout(15);

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMqttMessage);
  mqtt.setBufferSize(1024);      // room for polyline configs
  mqtt.setKeepAlive(30);
  mqtt.setSocketTimeout(15);

  connectWiFi();
}

// ============================================================================
//  LOOP
// ============================================================================

void loop() {
  // Feed the GPS parser as fast as bytes arrive
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }

  // MQTT housekeeping
  if (!mqtt.connected()) {
    if (millis() - lastMqttTry > MQTT_RETRY_MS) {
      lastMqttTry = millis();
      connectMQTT();
    }
  } else {
    mqtt.loop();
  }

  // WiFi watchdog
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastWifiTry > WIFI_RETRY_MS) {
      lastWifiTry = millis();
      connectWiFi();
    }
  }

  // Telemetry cadence
  if (mqtt.connected() && millis() - lastPublish >= cfg.publishMs) {
    lastPublish = millis();
    publishTelemetry();
  }

  // Status LED
  static uint32_t ledTick = 0;
  if (millis() - ledTick > 500)
   {
    ledTick = millis();
    if (WiFi.status() != WL_CONNECTED)      digitalWrite(STATUS_LED, LOW);
    else if (!gps.location.isValid())       digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));
    else                                    digitalWrite(STATUS_LED, HIGH);
  }
}

// ============================================================================
//  WIFI / MQTT
// ============================================================================

void connectWiFi()
 {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.printf("WiFi: connecting to %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(WIFI_HOST);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) 
  {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED)
   {
    Serial.printf("WiFi OK   IP %s   RSSI %d dBm\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
  }
   else 
  {
    Serial.println("WiFi failed — will retry");
  }
}

void connectMQTT()
 {
  if (WiFi.status() != WL_CONNECTED) return;

  String clientId = String("esp32_") + DEVICE_ID + "_" +
                    String((uint32_t)esp_random(), HEX);

  Serial.printf("MQTT: connecting %s:%d as %s\n",
                MQTT_HOST, MQTT_PORT, clientId.c_str());

  bool ok;
  if (strlen(MQTT_USER) > 0)
   {
    ok = mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASS,
                      MQTT_TOPIC, 0, true, "{\"state\":\"offline\"}");
  }
   else 
  {
    ok = mqtt.connect(clientId.c_str(), nullptr, nullptr,
                      MQTT_TOPIC, 0, true, "{\"state\":\"offline\"}");
  }

  if (!ok) 
  {
    Serial.printf("MQTT failed rc=%d\n", mqtt.state());
    return;
  }
  Serial.println("MQTT connected");

  String cfgTopic = String(MQTT_TOPIC) + "/cfg";
  if (mqtt.subscribe(cfgTopic.c_str(), 1))
    Serial.printf("Subscribed to %s\n", cfgTopic.c_str());

  // Retained "online" hello
  StaticJsonDocument<128> hello;
  hello["state"] = "online";
  hello["id"]    = DEVICE_ID;
  hello["fw"]    = FIRMWARE;
  char buf[128];
  serializeJson(hello, buf);
  mqtt.publish(MQTT_TOPIC, buf, true);
}

// ============================================================================
//  MQTT INBOUND — dashboard config
// ============================================================================

void onMqttMessage(char* topic, byte* payload, unsigned int len)
 {
  if (len == 0 || len > 900) return;

  DynamicJsonDocument doc(1024);
  DeserializationError err = deserializeJson(doc, payload, len);
  if (err) {
    Serial.printf("JSON parse error: %s\n", err.c_str());
    return;
  }
  JsonObjectConst obj = doc.as<JsonObjectConst>();
  if (obj.isNull()) return;
  parseConfig(obj);
}

void parseConfig(JsonObjectConst d) 
{
  const char* type = d["type"] | "";

  // ---- Global settings ----
  if (strcmp(type, "settings") == 0)
   {
    if (d.containsKey("interval"))
      cfg.publishMs = (uint32_t)(d["interval"].as<float>() * 1000.0f);
    if (d.containsKey("maxSpeed")) cfg.maxKph  = d["maxSpeed"].as<float>();
    if (d.containsKey("lowBat"))   cfg.lowBatV = d["lowBat"].as<float>();
    if (d.containsKey("name"))     cfg.deviceName = d["name"].as<const char*>();
    Serial.printf("Settings: interval=%ums max=%.1f lowBat=%.2f\n",
                  cfg.publishMs, cfg.maxKph, cfg.lowBatV);
    return;
  }

  // ---- Geofence ----
  if (strcmp(type, "circle") == 0 ||
      strcmp(type, "polygon") == 0 ||
      strcmp(type, "route") == 0) {

    uint8_t id = d["id"] | 0;
    int slot = -1, freeSlot = -1;
    for (int i = 0; i < MAX_GEOFENCES; ++i) 
    {
      if (geofences[i].used && geofences[i].id == id) { slot = i; break; }
      if (!geofences[i].used && freeSlot < 0) freeSlot = i;
    }
    if (slot < 0) slot = freeSlot;
    if (slot < 0) {
      Serial.println("Geofence table full — dropping packet");
      return;
    }

    Geofence& g = geofences[slot];
    g.used      = true;
    g.id        = id;
    g.enabled   = d["enabled"] | true;
    g.radius    = d["radius"] | 0.0f;
    g.wasInside = -1;

    const char* nm = d["name"] | "";
    strncpy(g.name, nm, sizeof(g.name) - 1);
    g.name[sizeof(g.name) - 1] = 0;

    const char* col = d["color"] | "#38bdf8";
    strncpy(g.color, col, sizeof(g.color) - 1);
    g.color[sizeof(g.color) - 1] = 0;

    if (strcmp(type, "circle") == 0) 
    {
      g.type = GeoType::Circle;
      g.lat0 = d["lat"] | 0.0;
      g.lng0 = d["lng"] | 0.0;
      g.nPoints = 0;
    } 
    else
     {
      g.type = (strcmp(type, "polygon") == 0)
                 ? GeoType::Polygon : GeoType::Route;

      JsonArrayConst pts = d["points"].as<JsonArrayConst>();
      g.nPoints = 0;
      for (JsonArrayConst p : pts)
       {
        if (g.nPoints >= MAX_POLY_POINTS) break;
        g.lat[g.nPoints] = p[0].as<double>();
        g.lng[g.nPoints] = p[1].as<double>();
        g.nPoints++;
      }
      if (g.nPoints < 2)
       {
        g.used = false;
        Serial.println("Rejected geofence (too few points)");
        return;
      }
    }
    Serial.printf("Geofence id=%u type=%s name=%s pts=%u r=%.0f\n",
                  g.id, type, g.name, g.nPoints, g.radius);
  }
}

// ============================================================================
//  TELEMETRY
// ============================================================================

float readBatteryVolts() {
  uint16_t s[7];
  for (int i = 0; i < 7; ++i) { s[i] = analogRead(BAT_ADC_PIN); delay(2); }
  // insertion sort
  for (int i = 1; i < 7; ++i)
    for (int j = i; j > 0 && s[j] < s[j-1]; --j)
      { uint16_t t = s[j]; s[j] = s[j-1]; s[j-1] = t; }
  float raw = s[3];
  return (raw / 4095.0f) * 3.3f * BAT_DIVIDER * 1.03f;
}

void publishTelemetry()
 {
  StaticJsonDocument<512> doc;

  doc["id"]   = DEVICE_ID;
  doc["name"] = cfg.deviceName;
  doc["fw"]   = FIRMWARE;
  doc["ip"]   = WiFi.localIP().toString();
  doc["rssi"] = WiFi.RSSI();
  doc["bat"]  = serialized(String(readBatteryVolts(), 2));
  doc["temp"] = temperatureRead();

  bool fix = gps.location.isValid() &&
             gps.location.age() < GPS_STALE_MS &&
             gps.satellites.value() >= 4;

  if (fix)
   {
    double lat = gps.location.lat();
    double lng = gps.location.lng();

    doc["lat"]     = serialized(String(lat, 6));
    doc["lng"]     = serialized(String(lng, 6));
    doc["alt"]     = gps.altitude.isValid() ? gps.altitude.meters() : 0;
    doc["speed"]   = gps.speed.isValid()    ? gps.speed.kmph()     : 0;
    doc["heading"] = gps.course.isValid()   ? gps.course.deg()     : 0;
    doc["sat"]     = gps.satellites.value();
    doc["acc"]     = gps.hdop.isValid()     ? gps.hdop.hdop()*5.0 : 0;
    doc["fix"]     = 1;

    if (gps.time.isValid() && gps.date.isValid()) 
    {
      char ts[32];
      snprintf(ts, sizeof(ts), "%04d-%02d-%02d %02d:%02d:%02d",
               gps.date.year(), gps.date.month(), gps.date.day(),
               gps.time.hour(), gps.time.minute(), gps.time.second());
      doc["time"] = ts;
    }

    // --- Event engine ---
    float bv = readBatteryVolts();
    if (bv < cfg.lowBatV && !lowBatLatch)
     {
      lowBatLatch = true;
      publishEvent("Low Battery", String(bv, 2) + " V", "warn");
    } else if (bv >= cfg.lowBatV + 0.1f) {
      lowBatLatch = false;
    }

    float kph = gps.speed.isValid() ? gps.speed.kmph() : 0;
    if (kph > cfg.maxKph && !overSpdLatch) 
    {
      overSpdLatch = true;
      publishEvent("Overspeed", String(kph, 0) + " km/h", "warn");
    }
     else if (kph < cfg.maxKph - 5.0f)
      {
      overSpdLatch = false;
    }

    checkGeofences(lat, lng);
    wasOnline = true;
  }
   else
   {
    doc["fix"] = 0;
    if (wasOnline) {
      wasOnline = false;
      publishEvent("GPS Lost", "No valid fix", "error");
    }
  }

  char out[512];
  size_t n = serializeJson(doc, out, sizeof(out));
  if (!mqtt.publish(MQTT_TOPIC, (const uint8_t*)out, n, false))
    Serial.println("MQTT publish failed");
}

void publishEvent(const String& type, const String& msg, const char* level)
 {
  StaticJsonDocument<256> d;
  d["event"] = type;
  d["msg"]   = msg;
  d["level"] = level;
  d["ts"]    = millis();
  char buf[256];
  serializeJson(d, buf);
  String t = String(MQTT_TOPIC) + "/events";
  mqtt.publish(t.c_str(), buf);
  Serial.printf("[EVENT] %s — %s\n", type.c_str(), msg.c_str());
}

void checkGeofences(double lat, double lng)
 {
  for (auto& g : geofences) 
  {
    if (!g.used || !g.enabled) continue;

    bool inside = isInside(g, lat, lng);
    int8_t now  = inside ? 1 : 0;

    if (g.wasInside < 0) 
    {        // first sample after boot
      g.wasInside = now;
      continue;
    }
    if (now != g.wasInside)
     {
      publishEvent(inside ? "Entered Geofence" : "Exited Geofence",
                   g.name, inside ? "success" : "warn");
      g.wasInside = now;
    }
  }
}

#include <TinyGPS++.h>
#include <SoftwareSerial.h>

TinyGPSPlus gps;
SoftwareSerial gpsSerial(4, 3);   // RX, TX

void setup()
{
  Serial.begin(115200);
  gpsSerial.begin(9600);
}

void loop()
{
  while (gpsSerial.available())
  {
    gps.encode(gpsSerial.read());
  }

  static unsigned long lastUpdate = 0;

  // Print every second
  if (millis() - lastUpdate >= 1000)
  {
    lastUpdate = millis();

    if (!gps.location.isValid())
    {
      Serial.println("Waiting for GPS Fix...");
      return;
    }

    Serial.println("================================");

    // Location
    Serial.print("Latitude  : ");
    Serial.println(gps.location.lat(), 6);

    Serial.print("Longitude : ");
    Serial.println(gps.location.lng(), 6);

    // Altitude
    if (gps.altitude.isValid())
    {
      Serial.print("Altitude  : ");
      Serial.print(gps.altitude.meters(), 2);
      Serial.println(" m");
    }
    else
    {
      Serial.println("Altitude  : N/A");
    }

    // Speed
    if (gps.speed.isValid())
    {
      Serial.print("Speed     : ");
      Serial.print(gps.speed.kmph(), 2);
      Serial.println(" km/h");
    }

    // Course
    if (gps.course.isValid())
    {
      Serial.print("Heading   : ");
      Serial.print(gps.course.deg(), 1);
      Serial.print(" (");
      Serial.print(TinyGPSPlus::cardinal(gps.course.deg()));
      Serial.println(")");
    }

    // Satellites
    if (gps.satellites.isValid())
    {
      Serial.print("Satellites: ");
      Serial.println(gps.satellites.value());
    }

    // HDOP
    if (gps.hdop.isValid())
    {
      Serial.print("HDOP      : ");
      Serial.println(gps.hdop.hdop(), 2);
    }

    // UTC Date & Time
    if (gps.date.isValid() && gps.time.isValid())
    {
      Serial.print("UTC       : ");

      Serial.print(gps.date.day());
      Serial.print("/");
      Serial.print(gps.date.month());
      Serial.print("/");
      Serial.print(gps.date.year());

      Serial.print(" ");

      if (gps.time.hour() < 10) Serial.print('0');
      Serial.print(gps.time.hour());
      Serial.print(":");

      if (gps.time.minute() < 10) Serial.print('0');
      Serial.print(gps.time.minute());
      Serial.print(":");

      if (gps.time.second() < 10) Serial.print('0');
      Serial.println(gps.time.second());
    }

    Serial.println("================================\n");
  }
}

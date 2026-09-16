#include <TinyGPS++.h>
#include <SoftwareSerial.h>

TinyGPSPlus gps;

SoftwareSerial gpsSerial(4, 3);   // RX, TX

void setup()
{
  Serial.begin(115200);
  gpsSerial.begin(9600);

  Serial.println("GPS Started...");
}

void loop()
{
  while (gpsSerial.available())
  {
    gps.encode(gpsSerial.read());
  }

  if (gps.location.isUpdated())
  {

    //--------------------------------------------------
    // BLOCK 1 : Latitude & Longitude
    //--------------------------------------------------

    Serial.println("========= LOCATION =========");

    Serial.print("Latitude : ");
    Serial.println(gps.location.lat(), 6);

    Serial.print("Longitude: ");
    Serial.println(gps.location.lng(), 6);




    //--------------------------------------------------
    // BLOCK 2 : Altitude
    //--------------------------------------------------

    Serial.println("\n========= ALTITUDE =========");

    Serial.print("Meters : ");
    Serial.println(gps.altitude.meters());

    Serial.print("Kilometers : ");
    Serial.println(gps.altitude.kilometers());

    Serial.print("Feet : ");
    Serial.println(gps.altitude.feet());

    Serial.print("Miles : ");
    Serial.println(gps.altitude.miles());




    //--------------------------------------------------
    // BLOCK 3 : Speed
    //--------------------------------------------------

    Serial.println("\n========= SPEED =========");

    Serial.print("km/h : ");
    Serial.println(gps.speed.kmph());

    Serial.print("mph : ");
    Serial.println(gps.speed.mph());

    Serial.print("m/s : ");
    Serial.println(gps.speed.mps());

    Serial.print("Knots : ");
    Serial.println(gps.speed.knots());




    //--------------------------------------------------
    // BLOCK 4 : Course (Direction)
    //--------------------------------------------------

    Serial.println("\n========= COURSE =========");

    Serial.print("Degrees : ");
    Serial.println(gps.course.deg());

    Serial.print("Cardinal : ");
    Serial.println(gps.course.cardinal());




    //--------------------------------------------------
    // BLOCK 5 : Date
    //--------------------------------------------------

    Serial.println("\n========= DATE =========");

    Serial.print("Day : ");
    Serial.println(gps.date.day());

    Serial.print("Month : ");
    Serial.println(gps.date.month());

    Serial.print("Year : ");
    Serial.println(gps.date.year());




    //--------------------------------------------------
    // BLOCK 6 : Time (UTC)
    //--------------------------------------------------

    Serial.println("\n========= TIME =========");

    Serial.print("Hour : ");
    Serial.println(gps.time.hour());

    Serial.print("Minute : ");
    Serial.println(gps.time.minute());

    Serial.print("Second : ");
    Serial.println(gps.time.second());

    Serial.print("Centisecond : ");
    Serial.println(gps.time.centisecond());




    //--------------------------------------------------
    // BLOCK 7 : Satellites
    //--------------------------------------------------

    Serial.println("\n========= SATELLITES =========");

    Serial.print("Satellite Count : ");
    Serial.println(gps.satellites.value());




    //--------------------------------------------------
    // BLOCK 8 : HDOP
    //--------------------------------------------------

    Serial.println("\n========= HDOP =========");

    Serial.print("HDOP : ");
    Serial.println(gps.hdop.value());






    //--------------------------------------------------
    // BLOCK 9 : Fix Age
    //--------------------------------------------------

    Serial.println("\n========= FIX AGE =========");

    Serial.print("Age(ms): ");
    Serial.println(gps.location.age());






    //--------------------------------------------------
    // BLOCK 10 : Valid Data
    //--------------------------------------------------

    Serial.println("\n========= VALID =========");

    Serial.print("Location : ");
    Serial.println(gps.location.isValid());

    Serial.print("Altitude : ");
    Serial.println(gps.altitude.isValid());

    Serial.print("Date : ");
    Serial.println(gps.date.isValid());

    Serial.print("Time : ");
    Serial.println(gps.time.isValid());






    //--------------------------------------------------
    // BLOCK 11 : Updated Data
    //--------------------------------------------------

    Serial.println("\n========= UPDATED =========");

    Serial.print("Location : ");
    Serial.println(gps.location.isUpdated());

    Serial.print("Altitude : ");
    Serial.println(gps.altitude.isUpdated());

    Serial.print("Date : ");
    Serial.println(gps.date.isUpdated());

    Serial.print("Time : ");
    Serial.println(gps.time.isUpdated());






    //--------------------------------------------------
    // BLOCK 12 : Characters Processed
    //--------------------------------------------------

    Serial.println("\n========= STATISTICS =========");

    Serial.print("Characters : ");
    Serial.println(gps.charsProcessed());

    Serial.print("Sentences : ");
    Serial.println(gps.sentencesWithFix());

    Serial.print("Failed Checksum : ");
    Serial.println(gps.failedChecksum());

    Serial.print("Passed Checksum : ");
    Serial.println(gps.passedChecksum());





    //--------------------------------------------------
    // BLOCK 13 : Distance Between Two Points
    //--------------------------------------------------

    double distance =
      TinyGPSPlus::distanceBetween(
      gps.location.lat(),
      gps.location.lng(),
      26.8467,      // Destination Latitude
      80.9462);     // Destination Longitude

    Serial.println("\n========= DISTANCE =========");

    Serial.print("Meters : ");
    Serial.println(distance);





    //--------------------------------------------------
    // BLOCK 14 : Course To Destination
    //--------------------------------------------------

    double course =
      TinyGPSPlus::courseTo(
      gps.location.lat(),
      gps.location.lng(),
      26.8467,
      80.9462);

    Serial.println("\n========= COURSE TO =========");

    Serial.print("Degrees : ");
    Serial.println(course);

    Serial.print("Direction : ");
    Serial.println(TinyGPSPlus::cardinal(course));





    //--------------------------------------------------
    // BLOCK 15 : Location Validity
    //--------------------------------------------------

    if(gps.location.isValid())
        Serial.println("GPS FIX OK");
    else
        Serial.println("Waiting for GPS Fix...");




    Serial.println("\n========================================\n");

  }
}

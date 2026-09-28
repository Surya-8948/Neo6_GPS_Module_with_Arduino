#include <TinyGPS++.h>
#include <SoftwareSerial.h>

static const int RXPin = 4;
static const int TXPin = 3;
static const uint32_t GPSBaud = 9600;

TinyGPSPlus gps;
SoftwareSerial ss(RXPin, TXPin);

// ==== COUNTERS ====
unsigned long totalBytes = 0;
unsigned long totalSentences = 0;
unsigned long lastByteTime = 0;
unsigned long bootTime = 0;
unsigned long lastReport = 0;

// Sentence type counters
int gpggaCount = 0, gpgrmcCount = 0, gpgsaCount = 0, gpgsvCount = 0, gpvtgCount = 0, otherCount = 0;

// NMEA sentence buffer
char nmea[120];
int nmeaIdx = 0;
bool nmeaStarted = false;

void setup() 
{
  Serial.begin(9600);
  ss.begin(GPSBaud);
  bootTime = millis();
  lastByteTime = millis();

  Serial.println(F("\n\n========================================="));
  Serial.println(F("   NEO-6M FULL HARDWARE DIAGNOSTIC"));
  Serial.println(F("========================================="));
  Serial.println(F("Power ON check shuru ho raha hai..."));
  Serial.println(F("GPS ko BAHAR ya khidki ke paas rakho!"));
  Serial.println(F("Ab 60 second wait karo...\n"));
  delay(3000);
}

void loop()
 {
  // NMEA parse karo
  while (ss.available() > 0)
   {
    char c = ss.read();
    totalBytes++;
    lastByteTime = millis();

    // Sentence detect karo
    if (c == '$') 
    {
      nmeaIdx = 0;
      nmeaStarted = true;
      nmea[nmeaIdx++] = c;
    } 
    else if (nmeaStarted && nmeaIdx < 118)
     {
      nmea[nmeaIdx++] = c;
      if (c == '\n') 
      {
        nmea[nmeaIdx] = '\0';
        totalSentences++;
        classifySentence(nmea);
        nmeaStarted = false;
        nmeaIdx = 0;
      }
    }

    gps.encode(c);
  }

  // Har 5 second me report
  if (millis() - lastReport > 5000) 
  {
    lastReport = millis();
    printFullReport();
  }
}

// Sentence ka type detect karo
void classifySentence(char *s) 
{
  if (strncmp(s, "$GPGGA", 6) == 0) gpggaCount++;
  else if (strncmp(s, "$GPRMC", 6) == 0) gpgrmcCount++;
  else if (strncmp(s, "$GPGSA", 6) == 0) gpgsaCount++;
  else if (strncmp(s, "$GPGSV", 6) == 0) gpgsvCount++;
  else if (strncmp(s, "$GPVTG", 6) == 0) gpvtgCount++;
  else otherCount++;
}

void printFullReport() 
{
  unsigned long uptime = (millis() - bootTime) / 1000;
  unsigned long silence = (millis() - lastByteTime) / 1000;

  Serial.println(F("\n========================================="));
  Serial.print(F("  REPORT @ ")); Serial.print(uptime); Serial.println(F(" sec"));
  Serial.println(F("========================================="));

  // ============================
  // TEST 1: TX LINE (GPS -> Arduino)
  // ============================
  Serial.println(F("\n[TEST 1] TX LINE (GPS se data aa raha?)"));
  Serial.print(F("  Total bytes: ")); Serial.println(totalBytes);
  Serial.print(F("  Silence duration: ")); Serial.print(silence); Serial.println(F(" sec"));

  if (totalBytes == 0 && uptime > 5) 
  {
    Serial.println(F("   FAIL: GPS se koi data nahi aa raha"));
    Serial.println(F("   Matlab: TX wire ya toota hai, ya ulta laga hai,"));
    Serial.println(F("   ya GPS ko power nahi mil rahi"));
    Serial.println(F("   Check: GPS TX -> Arduino D4 (RX)"));
    Serial.println(F("   Check: VCC 5V aur GND connected"));
  } 
  else if (silence > 3)
   {
    Serial.println(F("   WARN: Data aana ruk gaya (3+ sec silence)"));
    Serial.println(F("   Wire loose ho sakti hai ya power unstable hai"));
  }
   else if (totalBytes > 0) 
  {
    Serial.println(F("   PASS: GPS zinda hai, data flow ho raha hai"));
  }

  // ============================
  // TEST 2: CHIP ALIVE?
  // ============================
  Serial.println(F("\n[TEST 2] CHIP ALIVE CHECK"));
  Serial.print(F("  Total NMEA sentences: ")); Serial.println(totalSentences);

  if (totalSentences == 0 && totalBytes > 0) 
  {
    Serial.println(F("    Data aa raha hai lekin NMEA nahi hai"));
    Serial.println(F("   Baud rate galat ho sakta hai (9600 try karo)"));
  } 
  else if (totalSentences == 0) 
  {
    Serial.println(F("   Chip ka koi output nahi = chip dead ya power issue"));
  }
   else 
   {
    Serial.println(F("   PASS: Chip NMEA sentences bhej raha hai"));
  }

  // ============================
  // TEST 3: SENTENCE TYPES
  // ============================
  Serial.println(F("\n[TEST 3] SENTENCE TYPES (NMEA mix)"));
  Serial.print(F("  GPGGA (fix data): ")); Serial.println(gpggaCount);
  Serial.print(F("  GPRMC (min data) : ")); Serial.println(gpgrmcCount);
  Serial.print(F("  GPGSA (DOP)      : ")); Serial.println(gpgsaCount);
  Serial.print(F("  GPGSV (sats)     : ")); Serial.println(gpgsvCount);
  Serial.print(F("  GPVTG (speed)    : ")); Serial.println(gpvtgCount);
  Serial.print(F("  Other            : ")); Serial.println(otherCount);

  if (gpggaCount == 0 && gpgrmcCount == 0 && totalSentences > 10) 
  {
    Serial.println(F("   Standard NMEA nahi mil raha"));
    Serial.println(F("   Baud rate galat hai, ya module damaged"));
  } 
  else if (totalSentences > 0) {
    Serial.println(F("   PASS: Standard NMEA sentences mil rahe hain"));
  }

  // ============================
  // TEST 4: RX LINE (Arduino -> GPS)
  // ============================
  Serial.println(F("\n[TEST 4] RX LINE (Arduino se GPS tak)"));
  Serial.println(F("  ➜ Ye line code se direct test nahi hoti"));
  Serial.println(F("  ➜ Lekin agar TX OK hai, to mostly RX bhi OK hota hai"));
  Serial.println(F("  ➜ GPS ko command bhejne ke liye zaroori hai"));

  // ============================
  // TEST 5: ANTENNA / SIGNAL
  // ============================
  Serial.println(F("\n[TEST 5] ANTENNA / SIGNAL CHECK"));
  Serial.print(F("  Satellites in view: ")); Serial.println(gps.satellites.value());

  if (totalSentences > 20 && gps.satellites.value() == 0) 
  {
    Serial.println(F("  FAIL: Satellites 0 hain"));
    Serial.println(F("   Reason: Antenna nahi lagi, ya ghar ke andar ho"));
    Serial.println(F("   Fix: Antenna laga ke BAHAR le jao"));
  } 
  else if (gps.satellites.value() > 0 && gps.satellites.value() < 4) 
  {
    Serial.println(F("    Satellites kam hain (min 4 chahiye fix ke liye)"));
    Serial.println(F("   Khidki ke paas ya bahar rakho"));
  } 
  else if (gps.satellites.value() >= 4) 
  {
    Serial.println(F("   PASS: Satellites mil rahe hain"));
  }

  // ============================
  // TEST 6: FIX STATUS
  // ============================
  Serial.println(F("\n[TEST 6] FIX STATUS"));
  if (gps.location.isValid()) 
  {
    Serial.println(F("   FIX HO GAYA! "));
    Serial.print(F("  Lat: ")); Serial.println(gps.location.lat(), 6);
    Serial.print(F("  Lng: ")); Serial.println(gps.location.lng(), 6);
  }
   else 
  {
    Serial.println(F("   Fix nahi hua abhi"));
    if (gps.satellites.value() >= 4) {
      Serial.println(F("  ➜ Satellites hain, thoda wait karo (1-5 min)"));
    }
  }

  // ============================
  // TEST 7: CHECKSUM (Signal Quality)
  // ============================
  Serial.println(F("\n[TEST 7] SIGNAL QUALITY (Checksum)"));
  Serial.print(F("  Failed checksums: ")); Serial.println(gps.failedChecksum());
  Serial.print(F("  Passed sentences : ")); Serial.println(gps.passedChecksum());

  if (gps.failedChecksum() > gps.passedChecksum() / 2)
   {
    Serial.println(F("   Bahut zyada checksum fail"));
    Serial.println(F("   Noise, loose wire, ya power unstable"));
  } 
  else if (gps.passedChecksum() > 0) 
  {
    Serial.println(F("   Signal saaf hai"));
  }

  // ============================
  // TEST 8: BAUD RATE CHECK
  // ============================
  Serial.println(F("\n[TEST 8] BAUD RATE CHECK"));
  Serial.println(F("  Expected: 9600"));
  Serial.print(F("  Bytes/sec (approx): "));
  Serial.println(totalBytes / (uptime == 0 ? 1 : uptime));

  if (totalBytes > 0 && totalBytes < 50 && uptime > 10) 
  {
    Serial.println(F("    Bahut kam data — baud rate galat ho sakta hai"));
    Serial.println(F("   4800, 38400 bhi try karo"));
  } 
  else if (totalBytes > 200) {
    Serial.println(F("   Data rate theek hai"));
  }

  // ============================
  // FINAL VERDICT
  // ============================
  Serial.println(F("\n========================================="));
  Serial.println(F("  FINAL VERDICT"));
  Serial.println(F("========================================="));

  if (totalBytes == 0)
   {
    Serial.println(F("   CHIP DEAD / POWER ISSUE"));
    Serial.println(F("   VCC aur GND check karo"));
    Serial.println(F("   TX wire ulta laga kya?"));
  } 
  else if (gps.passedChecksum() == 0) 
  {
    Serial.println(F("   WRONG BAUD RATE"));
    Serial.println(F("   9600 ke alawa 4800/38400 try karo"));
  } else if (gps.satellites.value() == 0) 
  {
    Serial.println(F("   NO SATELLITE SIGNAL"));
    Serial.println(F("   Antenna check karo"));
    Serial.println(F("   BAHAR le jao, open sky me rakho"));
  }
   else if (!gps.location.isValid()) 
  {
    Serial.println(F("   ACQUIRING SATELLITES"));
    Serial.println(F("   Sab kuch theek hai, thoda wait karo"));
  } 
  else 
  {
    Serial.println(F("   ALL WORKING! GPS fix ho gaya!"));
  }
  Serial.println(F("=========================================\n"));
}

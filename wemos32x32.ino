#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <WiFiUdp.h>
#include <FastLED.h>

#define double_buffer
#define PxMATRIX_COLOR_DEPTH 4
#include <PxMatrix.h>

#include "IBMPlexBold10pt7b.h"

#include <Ticker.h>
#include <Timezone.h>
#include <ArduinoOTA.h>

#include <WiFiManager.h>

#define LOCK_VERSION       2
//#include <qrcode.h>

#include "AnimatedGIF.h"

AnimatedGIF gif;

TimeChangeRule CEST = {"CEST", Last, Sun, Mar, 2, 120};     // Central European Summer Time
TimeChangeRule CET = {"CET ", Last, Sun, Oct, 3, 60};       // Central European Standard Time
Timezone CE(CEST, CET);
TimeChangeRule *tcr;


#define P_LAT 16
#define P_A 5
#define P_B 4
#define P_C 15
#define P_D 12
#define P_E 0
#define P_OE 2


#define WIDTH 32
#define HEIGHT 32

Ticker display_ticker;

PxMATRIX display(WIDTH, HEIGHT, P_LAT, P_OE, P_A, P_B, P_C, P_D);

uint8_t display_draw_time=30; //10-50 is usually fine
float isrDelay = 0.004;

void display_updater() {
      display.display(display_draw_time);
 }

#define ACCESS_POINT "wemos32x32"

void configModeCallback (WiFiManager *myWiFiManager) {
  #if 0
  // generate QRCode and display it
  QRCode qrcode;
  uint8_t qrcodeBytes[qrcode_getBufferSize(LOCK_VERSION)];
  // 29x29
  qrcode_initText(&qrcode, qrcodeBytes, LOCK_VERSION, ECC_LOW, "WIFI:S:" ACCESS_POINT ";;");
  display.clearDisplay();
  uint8_t d = (WIDTH - qrcode.size) / 2;
  for (uint8_t y = 0; y < qrcode.size; ++y) {
    for (uint8_t x = 0; x < qrcode.size; ++x) {
      if (qrcode_getModule(&qrcode, x, y)) {
        display.drawPixelRGB888(x + d, y + d, 255, 255, 255);
      }
    }
  }
  display.showBuffer();
  #endif
}

unsigned int NTP_PORT = 2390;      // local port to listen for UDP packets
const char* ntpServerName = "time.google.com";
const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message
WiFiUDP udpNTP;

void sendNTPpacket(IPAddress& address) {
  if (WiFi.isConnected()) {
    byte packetBuffer[ NTP_PACKET_SIZE];
    memset(packetBuffer, 0, NTP_PACKET_SIZE);
    packetBuffer[0] = 0b11100011;   // LI, Version, Mode
    packetBuffer[1] = 0;     // Stratum, or type of clock
    packetBuffer[2] = 6;     // Polling Interval
    packetBuffer[3] = 0xEC;  // Peer Clock Precision
    // 8 bytes of zero for Root Delay & Root Dispersion
    packetBuffer[12] = 49;
    packetBuffer[13] = 0x4E;
    packetBuffer[14] = 49;
    packetBuffer[15] = 52;
      
    udpNTP.beginPacket(address, 123); //NTP requests are to port 123
    udpNTP.write(packetBuffer, NTP_PACKET_SIZE);
    udpNTP.endPacket();
  }
}

const time_t DEFAULT_TIME = 0;

time_t getNtpTime() {
  if (WiFi.isConnected()) {
    IPAddress timeServerIP; // time.nist.gov NTP server address
    WiFi.hostByName(ntpServerName, timeServerIP);
    while (udpNTP.parsePacket() > 0) ; // discard any previously received packets
    sendNTPpacket(timeServerIP);
    uint32_t beginWait = millis();
    while (millis() - beginWait < 1500) {
      int size = udpNTP.parsePacket();
      if (size >= NTP_PACKET_SIZE) {
 //       Serial.println("Receive NTP Response");
        byte packetBuffer[ NTP_PACKET_SIZE];
        udpNTP.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
        unsigned long secsSince1900;
        // convert four bytes starting at location 40 to a long integer
        secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
        secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
        secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
        secsSince1900 |= (unsigned long)packetBuffer[43];
        return secsSince1900 - 2208988800UL;
      }
    }
  }
//  Serial.println("No NTP Response :-(");
  return DEFAULT_TIME; // return 0 if unable to get the time
}

#define TPM2NET_IN_PORT 65506
#define TPM2NET_OUT_PORT 65442
WiFiUDP udpTPM2;


uint16_t myWHITE = display.color565(255, 255, 255);
uint16_t myBLACK = display.color565(0, 0, 0);
uint16_t myGREY = display.color565(188, 128, 128);
uint16_t myPINK = display.color565(0, 128, 128);

boolean otaStarted = false;

void initOTA() {
  ArduinoOTA.setHostname("Wemos32x32");
  ArduinoOTA.setPassword("wemos");
  ArduinoOTA.onStart([]() {
    display.setTextWrap(false);
     display.setTextColor(myWHITE, myBLACK);
   display.setFont();
   display.clearDisplay();
   otaStarted = true;
   });
  ArduinoOTA.onEnd([]() {
    display.clearDisplay();
   display.setCursor(2, 10);
   display.print("100 %");
   display.showBuffer();
    });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
   display.clearDisplay();
    unsigned int realprogress = (progress / (total / 100));
    if (realprogress < 100) {
    char buffer[6];
    buffer[0] = ' ';
    buffer[1] = (realprogress / 10) + '0';
   buffer[2] = (realprogress % 10) + '0';
   buffer[3] = ' ';
   buffer[4] = '%';
   buffer[5] = 0;
     display.setCursor(2, 10);
   display.print(buffer);
   yield();
   display.showBuffer();
    }
  });
  ArduinoOTA.onError([](ota_error_t error) {});
  ArduinoOTA.begin();
}

ESP8266WebServer webServer(80);     
void sendOk(String text = "Ok") { webServer.send(200, "text/plain", text); }


unsigned long lastFrameTime = 0;

uint8_t backgroundMode = 1; // plasma
boolean displayTime = true;
boolean smallClock = false;

void setup() {
  Serial.begin(115200);
 display.begin(16);
  gif.begin(LITTLE_ENDIAN_PIXELS);
  // DONOTUSE, bad 16 and 32 lines...
// display.setFastUpdate(true);
 display.setBrightness(220);
  display_ticker.attach(isrDelay, display_updater);

  WiFiManager wifiManager;
  // wifiManager.resetSettings();
  // TODO: increase in production
  wifiManager.setConfigPortalTimeout(60);
  wifiManager.setDebugOutput(true);
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setMinimumSignalQuality(20);
  wifiManager.autoConnect(ACCESS_POINT);

 if (WiFi.isConnected()) {
    udpNTP.begin(NTP_PORT);
    udpTPM2.begin(TPM2NET_IN_PORT);
    initOTA();

   webServer.on("/gif", HTTP_GET, []() { backgroundMode = 2; sendOk(); });
   webServer.on("/black", HTTP_GET, []() { backgroundMode = 0; sendOk(); });
   webServer.on("/grey", HTTP_GET, []() { backgroundMode = 5; sendOk(); });
   webServer.on("/plasma", HTTP_GET, []() { backgroundMode = 1; sendOk(); });
  webServer.on("/fire", HTTP_GET, []() { backgroundMode = 3; sendOk(); });
 webServer.on("/tpm2", HTTP_GET, []() { backgroundMode = 4; sendOk(); });
    webServer.on("/time", HTTP_GET, []() { displayTime = !displayTime; sendOk(); });
   webServer.on("/clocksize", HTTP_GET, []() { smallClock = !smallClock; sendOk(); });
  webServer.on("/info", HTTP_GET, []() { sendOk("gif\nblack\nplasma\nfire\ntpm2\ntime\nclocksize\n"); });
    webServer.begin();

    display.setFont();
    display.setCursor(0, 0);
    display.setTextColor(myWHITE);
    display.print(WiFi.localIP());
     display.showBuffer();
    delay(1000);
    setSyncInterval(3600);
   setSyncProvider(getNtpTime);
  }
}


void printCenter(String str, int16_t y) {
   display.setFont(&IBMPlexMono_Bold10pt7b);
  int16_t x = 4;
  display.setTextColor(myWHITE);
 display.setCursor(x - 1, y);
  display.print(str);
 yield();
 display.setCursor(x + 1, y);
  display.print(str);
 yield();
 display.setCursor(x, y - 1);
  display.print(str);
 display.setCursor(x, y + 1);
  display.print(str);
  yield();
  display.setTextColor(myBLACK);
  display.setCursor(x, y);
  display.print(str);
 yield();
}

void printSmall(String str, int16_t x) {
int16_t y = 2;
  display.setFont(NULL);
  display.setTextColor(myWHITE);
 display.setCursor(x - 1, y);
  display.print(str);
 yield();
 display.setCursor(x + 1, y);
  display.print(str);
yield();
 display.setCursor(x, y - 1);
  display.print(str);
yield();
display.setCursor(x, y + 1);
  display.print(str);
yield();
  display.setTextColor(myBLACK);
//  display.setTextColor(myPINK);
  display.setCursor(x, y);
 display.print(str);
 yield();
}

boolean flasher = true;

unsigned long lastTime = 0;

void displayTime2() {
   display.setTextWrap(false);
    time_t utc = now();
    time_t local = CE.toLocal(utc, &tcr);
    char buffer[6];
  uint8_t h = hour(local);
  buffer[0] = (h / 10) + '0';
  buffer[1] = (h  % 10) + '0';
  buffer[2] = 0;
    if (smallClock) {
      printSmall(buffer, 2);
    } else  {
      printCenter(buffer, 14);
    }
  uint8_t m = minute(local); 
   buffer[0] = (m / 10) + '0';
   buffer[1] = (m  % 10) + '0';
    buffer[2] = 0;
     if (smallClock) {
       if (millis() - lastTime >= 1000) {
          lastTime = millis();
          // query time here and blink
          flasher = !flasher;
       }
        printSmall(flasher ? ":" : " ", display.getCursorX());
        printSmall(buffer, display.getCursorX());
     } else  {
      printCenter(buffer, 30);
    }
}

const int startevening = 20;
const int endmorning = 8;

const int hoursinday = 24;
const uint8_t minbrightness = 128;
const uint8_t maxbrightness = 220;

uint8_t currentBrightness = maxbrightness; 
void setBrightnessFromHour() {
    uint8_t brightness = maxbrightness;
    time_t local = CE.toLocal(now(), &tcr);
    int hh = hour(local);
    if (hh >= startevening) {
      // distance from startevening
      int d = hh - startevening;
      // map distance to 
      brightness = (uint8_t)map(d, 0, (hoursinday - startevening), maxbrightness, minbrightness);
    } else if (hh <= 8) {
      // distance fromm endsmorning
      int d = endmorning - hh;
      brightness = (uint8_t)map(d, 0, endmorning, maxbrightness, minbrightness);
    }
    if (currentBrightness != brightness) {
      display.setBrightness(brightness);
      currentBrightness = brightness;
    }
}

uint16_t PlasmaTime = 0;
uint16_t PlasmaShift = (random8(0, 5) * 32) + 64;

#define PLASMA_X_FACTOR     24
#define PLASMA_Y_FACTOR     24
#define TARGET_FRAME_TIME   20  // Desired update rate, though if too many leds it will just run as fast as it can!

uint32_t  LoopDelayMS = TARGET_FRAME_TIME;
uint32_t  LastLoop = millis() - LoopDelayMS;

uint8_t defaultValue = 128;

void backgroundPlasma2() {
    // Fill background with dim plasma
  int16_t r = sin16(PlasmaTime) / 256;
  int16_t rr = cos16(-PlasmaTime) / 512;
  for (int16_t y = 0; y < HEIGHT; y++) {
      yield();
      int16_t ry = cos16(y * (-r) * PLASMA_Y_FACTOR + PlasmaTime);
     for (int16_t x = 0; x < WIDTH; x++) {
          int16_t h = sin16(x * r * PLASMA_X_FACTOR + PlasmaTime) + ry + sin16(y * x * rr);
        CRGB color = CHSV((uint8_t)((h / 256) + 128), 250, defaultValue);
       display.drawPixelRGB888(x, y, color.r, color.g, color.b);
      }
    }
  if ((millis() - LastLoop) >= LoopDelayMS) {
    LastLoop = millis();
    uint16_t OldPlasmaTime = PlasmaTime;
    PlasmaTime += PlasmaShift;
    if (OldPlasmaTime > PlasmaTime)
      PlasmaShift = (random8(0, 5) * 32) + 64;
  }
  yield();
}

static void GIFDraw(int x, int y, int w, int h, uint16_t* lBuf) {
  // h == 1
  for (int idx = 0; idx < w; ++idx) {
    display.drawPixelRGB565(x + idx, y, lBuf[idx]);
  }
}

#define DISPLAY_WIDTH 32

// draws one line
void GIFDraw(GIFDRAW *pDraw) {
    uint8_t *s;
    uint16_t *d, *usPalette, usTemp[DISPLAY_WIDTH]; // should be DISPLAY_WIDTH
    int x, y, iWidth;

    iWidth = pDraw->iWidth;
    if (iWidth > DISPLAY_WIDTH)
       iWidth = DISPLAY_WIDTH;
    usPalette = pDraw->pPalette;
    y = pDraw->iY + pDraw->y; // current line
    
    s = pDraw->pPixels;
    if (pDraw->ucDisposalMethod == 2) { // restore to background color
      for (x=0; x<iWidth; x++) {
        if (s[x] == pDraw->ucTransparent)
           s[x] = pDraw->ucBackground;
      }
      pDraw->ucHasTransparency = 0;
    }
    // Apply the new pixels to the main image
    if (pDraw->ucHasTransparency) { // if transparency used 
      uint8_t *pEnd, c, ucTransparent = pDraw->ucTransparent;
      int x, iCount;
      pEnd = s + iWidth;
      x = 0;
      iCount = 0; // count non-transparent pixels
      while(x < iWidth) {
        c = ucTransparent-1;
        d = usTemp;
        while (c != ucTransparent && s < pEnd) {
          c = *s++;
          if (c == ucTransparent) { // done, stop 
            s--; // back up to treat it like transparent
          }
          else { // opaque 
             *d++ = usPalette[c];
             iCount++;
          }
        } // while looking for opaque pixels
        if (iCount) { // any opaque pixels? 
          GIFDraw( pDraw->iX + x, y, iCount, 1, (uint16_t*)usTemp);
          x += iCount;
          iCount = 0;
        }
        // no, look for a run of transparent pixels
        c = ucTransparent;
        while (c == ucTransparent && s < pEnd) {
          c = *s++;
          if (c == ucTransparent)
             iCount++;
          else
             s--; 
        }
        if (iCount) {
          x += iCount; // skip these
          iCount = 0;
        }
      }
    } else {
      s = pDraw->pPixels;
      // Translate the 8-bit pixels through the RGB565 palette (already byte reversed)
      for (x=0; x<iWidth; x++)
        usTemp[x] = usPalette[*s++];
        GIFDraw( pDraw->iX, y, iWidth, 1, (uint16_t*)usTemp );
    }
}

unsigned long backgroundMillis = 0;
unsigned long animLength = 10; // play each gif for 10 seconds
boolean imageLoaded = false;

#define MAX_GIF_SIZE  2000
unsigned char gifinmemory[MAX_GIF_SIZE];

void loadNewImage2(const char* host, const char* rest) {
    if (WiFi.isConnected()) {
    WiFiClient wclient;
    unsigned int size = -1;
    if (wclient.connect(host, 80)) { 
      wclient.print(String(F("GET ")) + rest + " HTTP/1.1\r\n" + "Host: " + host + "\r\n" + "Connection: close\r\n\r\n");
            while (wclient.connected() && !wclient.available()) {
              delay(10);
            }
            while (wclient.connected()) {
              String line = wclient.readStringUntil('\n');
              // parse size
              int headerSep = line.indexOf(':');
              if (headerSep > 0) {
                 String name = line.substring(0, headerSep);
                String value = line.substring(headerSep + 1);
                value.trim();
                if (name.equalsIgnoreCase(F("Content-Length"))) {
                   size = value.toInt();
               } 
              }
//             Serial.println(line);
              if (line == "\r") {
                  break;
              }
          }
 //         Serial.println("Read headers");
          if (size < MAX_GIF_SIZE) {
            int pos = 0;
            char buffer[256];
 //           Serial.println(String("Size expected: ") + size);
            while ((pos < (size - 1))) {
                int readbytes = wclient.read(buffer, 255);
  //              Serial.print(String("Read: ") + readbytes);
                 memcpy(gifinmemory + pos, buffer, readbytes);
                 Serial.println(String(F("   Total: ")) + pos);
               pos += readbytes;
             } 
           gif.close();
            imageLoaded = gif.open(gifinmemory, size, GIFDraw); 
          } else {
            imageLoaded = false;
            }
          wclient.stop();
       } 
   }
}


void playGIFFrame() {
  if (imageLoaded) {
    gif.playFrame(true, NULL);
  } else {
    // no image loaded
  //fill_solid(colorsBACK, NUM_LEDS, CRGB::Red);
  }
}

void backgroundGIF() {
   if (((millis() - backgroundMillis) >= (animLength * 1000)) || !imageLoaded) { // load new image
         backgroundMillis = millis();
         display.clearDisplay();
         loadNewImage2("merlinux.free.fr", "/gif32/gif2.php"); 
   }
   playGIFFrame();
}

const int rows = HEIGHT;
const int cols = WIDTH;
/* Flare constants */
const uint8_t flarerows = 4;    /* number of rows (from bottom) allowed to flare */
const uint8_t maxflare = 2;     /* max number of simultaneous flares */
const uint8_t flarechance = 25; /* chance (%) of a new flare (if there's room) */
const uint8_t flaredecay = 14;  /* decay rate of flare radiation; 14 is good */

/* This is the map of colors from coolest (black) to hottest. Want blue flames? Go for it! */
const uint32_t colors[] = {
  0x000000,
  0x000000,
  0x100000,
  0x200000,
  0x300000,
  0x400000,
  0x600000,
  0x700000,
  0x800000,
  0x900000,
  0xA00000,
  0xB00000,
  0xC02000,
  0xC02000,
  0xC03000,
  0xC03000,
  0xC04000,
  0xC04000,
  0xC05000,
  0xC06000,
  0xC07000,
  0xC08000,
  0xC08000,
  0xC08000,
  0x807080,
  0x807080,
  0x807080
};
const uint8_t NCOLORS = (sizeof(colors)/sizeof(colors[0]));

uint8_t pix[rows][cols];
uint8_t nflare = 0;
uint32_t flare[maxflare];

uint32_t isqrt(uint32_t n) {
  if ( n < 2 ) return n;
  uint32_t smallCandidate = isqrt(n >> 2) << 1;
  uint32_t largeCandidate = smallCandidate + 1;
  return (largeCandidate*largeCandidate > n) ? smallCandidate : largeCandidate;
}

// Set pixels to intensity around flare
void glow( int x, int y, int z ) {
  int b = z * 10 / flaredecay + 1;
  for ( int i=(y-b); i<(y+b); ++i ) {
    for ( int j=(x-b); j<(x+b); ++j ) {
      if ( i >=0 && j >= 0 && i < rows && j < cols ) {
        int d = ( flaredecay * isqrt((x-j)*(x-j) + (y-i)*(y-i)) + 5 ) / 10;
        uint8_t n = 0;
        if ( z > d ) n = z - d;
        if ( n > pix[i][j] ) { // can only get brighter
          pix[i][j] = n;
        }
      }
    }
  }
}

void newflare() {
  if ( nflare < maxflare && random(1,101) <= flarechance ) {
    int x = random(0, cols);
    int y = random(0, flarerows);
    int z = NCOLORS - 1;
    flare[nflare++] = (z<<16) | (y<<8) | (x&0xff);
    glow( x, y, z );
  }
}

/** make_fire() animates the fire display. It should be called from the
 *  loop periodically (at least as often as is required to maintain the
 *  configured refresh rate). Better to call it too often than not enough.
 *  It will not refresh faster than the configured rate. But if you don't
 *  call it frequently enough, the refresh rate may be lower than
 *  configured.
 */
void backgroundFire() {
  uint16_t i, j;
 
  // First, move all existing heat points up the display and fade
  for ( i=rows-1; i>0; --i ) {
    yield();
    for ( j=0; j<cols; ++j ) {
      uint8_t n = 0;
      if ( pix[i-1][j] > 0 )
        n = pix[i-1][j] - 1;
      pix[i][j] = n;
    }
  }

  // Heat the bottom row
  for ( j=0; j<cols; ++j ) {
    i = pix[0][j];
    if ( i > 0 ) {
      pix[0][j] = random(NCOLORS-10, NCOLORS-2);
    }
  }

  // flare
  for ( i=0; i<nflare; ++i ) {
    int x = flare[i] & 0xff;
    int y = (flare[i] >> 8) & 0xff;
    int z = (flare[i] >> 16) & 0xff;
    glow( x, y, z );
    if ( z > 1 ) {
      flare[i] = (flare[i] & 0xffff) | ((z-1)<<16);
    } else {
      // This flare is out
      for ( int j=i+1; j<nflare; ++j ) {
        flare[j-1] = flare[j];
      }
      --nflare;
    }
    yield();
  }
  newflare();

  // Set and draw
  for ( i=0; i<rows; ++i ) {
    for ( j=0; j<cols; ++j ) {
      // colorsBACK[XY(j,rows - i - 1)] = colors[pix[i][j]];
      uint32_t color = colors[pix[i][j]];
      uint8_t* bcolor = (uint8_t*)&color;
      display.drawPixelRGB888(j, rows - i - 1, bcolor[2], bcolor[1], bcolor[0]);
    }
  }
 }

const uint16_t numleds = WIDTH * HEIGHT;

const uint8_t packet_start_byte = 0x9c;
const uint8_t packet_type_data = 0xda;
const uint8_t packet_type_cmd = 0xc0;
const uint8_t packet_type_response = 0xaa;
const uint8_t packet_end_byte = 0x36;
const uint8_t packet_response_ack = 0xac;

// maximum udp esp8266 payload = 1460 -> 484 rgb leds per frame (22x22 matrix)
// then you have to use packet numbers and split the payload
const uint8_t rowsPerFrame = 8;
const uint8_t frames = HEIGHT / rowsPerFrame;
const uint16_t payload = WIDTH * rowsPerFrame * 3;
const uint16_t expected_packet_size = payload + 7;

//uint8_t data[payload];
// reuse gif buffer
uint8_t* data = gifinmemory;

const void sendAck() {
  udpTPM2.beginPacket(udpTPM2.remoteIP(), TPM2NET_OUT_PORT);
  udpTPM2.write(&packet_response_ack, 1);
  udpTPM2.endPacket();
}

boolean showb = false;

 void backgroundTPM2() {
  uint16_t packet_size = udpTPM2.parsePacket();
  if (packet_size == 0) return;
  if (showb) return;
     // then parse to check
  if (udpTPM2.read() != packet_start_byte) return;
  // packet type
  uint8_t ptype = udpTPM2.read();
  uint16_t frame_size = (udpTPM2.read() << 8) | udpTPM2.read();
  // skip packet number and number of packets
  uint8_t packet = udpTPM2.read() - 1; //  starts at 1
  uint8_t npackets = udpTPM2.read();

  switch (ptype) {
    case packet_type_response: sendAck(); break;

    case packet_type_data: {
      if (frame_size != payload)
        return;
       Serial.println(String(F("Packet ")) + packet + " / " + npackets + "(" + packet_size + ")");

      uint16_t offset = payload * packet;
        udpTPM2.read(data, payload);
        unsigned char* sdata = data;
        // then write to offset in real display
        for (uint8_t y = 0; y < rowsPerFrame; ++y) {
          yield();
          uint8_t starty = y + rowsPerFrame * packet;
 //           Serial.println(String("Start y ") + starty);
          for (uint8_t x = 0; x < WIDTH; ++x) {
            yield();
            uint8_t r = *sdata++;
            uint8_t g = *sdata++;
            uint8_t b = *sdata++;
            display.drawPixelRGB888(x, starty, r, g, b);
          }
        } 
        showb = packet == (npackets - 1); 
       break;
    }
    case packet_type_cmd: break;
    }
  // skip end byte, maybe not neccessary
  udpTPM2.read();
}

int prevMode = backgroundMode;

void loop() {
   ArduinoOTA.handle();
   webServer.handleClient();
   if (!otaStarted) {
    setBrightnessFromHour();
     if (prevMode != backgroundMode) {
     display.fillRect(0, 0, 32, 32, myBLACK);
      prevMode = backgroundMode;
     }
      switch (backgroundMode) {
       case 1: backgroundPlasma2(); break;
       case 2: backgroundGIF(); break;
       case 3: backgroundFire(); break;
       case 4: backgroundTPM2(); break;
       case 5: display.fillRect(0, 0, 32, 32, myGREY); break;
      default: display.fillRect(0, 0, 32, 32, myBLACK); break;
     }
     if (displayTime) 
        displayTime2();
      if ((backgroundMode != 4) || showb) {
        display.showBuffer();
        if (backgroundMode == 4) showb = false;
      }
   }
    yield();
}

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <WiFiUdp.h>
#include <FastLED.h>
#define double_buffer
// saves A  LOT of memory ! (default are 64... and there are two buffers with 3 byte / pixel
#define PxMATRIX_MAX_HEIGHT 32
#define PxMATRIX_MAX_WIDTH 32
#include <PxMatrix.h>
//#include <Fonts/FreeSansBold9pt7b.h>
#include "IBMPlexBold10pt7b.h"

#include <Ticker.h>
#include <Timezone.h>
#include <ArduinoOTA.h>

#if defined(USEFS)
//#include <FS.h>
#include <LittleFS.h>
#define FILESYSTEM LittleFS
//#define FILESYSTEM SPIFFS
#endif

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
#define NUM_LEDS (WIDTH * HEIGHT)
#define BORDER_WIDTH 2

CRGB colorsBACK[NUM_LEDS];

Ticker display_ticker;

PxMATRIX display(WIDTH, HEIGHT, P_LAT, P_OE, P_A, P_B, P_C, P_D);
//PxMATRIX display(WIDTH, HEIGHT, P_LAT, P_OE, P_A, P_B, P_C, P_D, P_E);

uint8_t display_draw_time=30; //10-50 is usually fine
float isrDelay = 0.002;

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

void initOTA() {
  ArduinoOTA.setHostname("WemosMatrix32");
  ArduinoOTA.setPassword("wemos");
  ArduinoOTA.onStart([]() {
     display.clearDisplay();
    display.setFont();
    display.showBuffer();
  });
  ArduinoOTA.onEnd([]() {});
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    unsigned int realprogress = (progress / (total / 100));
    char buffer[8];
    sprintf(buffer, "%.2d %%", realprogress);
    display.setCursor(2, 10);
    display.clearDisplay();
    display.print(buffer);
   display.showBuffer();
  });
  ArduinoOTA.onError([](ota_error_t error) {});
  ArduinoOTA.begin();
}

ESP8266WebServer webServer(80);       // Create a webserver object that listens for HTTP request on port 80

#if defined(USEFS)
String fileList() {
      String str = "";
  Dir dir = FILESYSTEM.openDir("/");
  while (dir.next()) {
    str += dir.fileName();
    str += " -> ";
    str += dir.fileSize();
    str += " b\r\n";
  }
  return str;
}
#endif

void sendOk(String text = "Ok") {
  webServer.send(200, "text/plain", text);
}

unsigned long lastFrameTime = 0;

int backgroundMode = 5; // fire
int timeMode = 1;
unsigned long animLength = 20; // play each gif for 20 seconds


uint16_t myWHITE = display.color565(255, 255, 255);
uint16_t myBLACK = display.color565(0, 0, 0);
uint16_t myGREY = display.color565(16, 16, 16);


void setup() {
  Serial.begin(115200);
#if defined(USEFS)
  FILESYSTEM.begin();
#endif
  display.begin(16);
  gif.begin(LITTLE_ENDIAN_PIXELS);
 display.setFastUpdate(true);
   display.setBrightness(254);
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
    initOTA();

   webServer.on("/intensity", HTTP_GET, []() {
      String value = webServer.arg("value");
      display.setBrightness(value.toInt());
      sendOk();
    });
   webServer.on("/animlen", HTTP_GET, []() {
      String value = webServer.arg("value");
      animLength = value.toInt();
      sendOk();
    });
   webServer.on("/bmode", HTTP_GET, []() {
      String mode = webServer.arg("value");
      backgroundMode = mode.toInt();
      sendOk();
    });
    webServer.on("/tmode", HTTP_GET, []() {
      String mode = webServer.arg("value");
      timeMode = mode.toInt();
      sendOk();
    });
    #if defined(USEFS)
    webServer.on("/format", HTTP_GET, []() { boolean ret = FILESYSTEM.format(); sendOk(ret ? "Success" : "Failure"); });
    webServer.on("/list", HTTP_GET, []() { sendOk(fileList()); });
    #endif
    webServer.begin();
    display.setFont();
    display.setCursor(0, 0);
    display.setTextColor(myWHITE);
    display.print(WiFi.localIP());
    display.showBuffer();
    delay(2000);
    setSyncInterval(3600);
   setSyncProvider(getNtpTime);
    delay(4000);
  }
}


void printCenter(String str, int16_t y) {
#if 0
  int16_t  x1, y1;
  uint16_t w, h;
  display.getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(((WIDTH - w) / 2) - x1, y);
#else
int16_t x = 4;
#if 1
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
#endif
yield();
  display.setTextColor(myBLACK);
  display.setCursor(x, y);
  display.print(str);
#endif
}

#if 1
uint16_t XY(uint8_t x, uint8_t y) {
  return y * WIDTH + x;
}
#else
// alternate (zigzag)
uint16_t XY(uint8_t x, uint8_t y) {
  uint16_t i;
  if( y & 0x01) {
      // Odd rows run backwards
      uint8_t reverseX = (WIDTH - 1) - x;
      i = (y * WIDTH) + reverseX;
    } else {
      // Even rows run forwards
      i = (y * WIDTH) + x;
    }
    return i;
}
#endif

void displayFastLED(CRGB* colors) {
    for (uint16_t y = 0; y < HEIGHT; ++y) {
      yield();
      for (uint16_t x = 0; x < WIDTH; ++x) {
        CRGB& color = colors[XY(x, y)];
        display.drawPixelRGB888(x, y, color.r, color.g, color.b);
      }
    }
}

void displayTime() {
 display.setFont(&IBMPlexMono_Bold10pt7b);
   display.setTextWrap(false);
   display.setTextColor(myWHITE);
    time_t utc = now();
    time_t local = CE.toLocal(utc, &tcr);
    char buffer[4];
    sprintf(buffer, "%.2d", hour(local));
    printCenter(buffer, 14);
   sprintf(buffer, "%.2d", minute(local));     
    printCenter(buffer, 30);
}


uint16_t PlasmaTime = 0;
uint16_t PlasmaShift = (random8(0, 5) * 32) + 64;

#define PLASMA_X_FACTOR     24
#define PLASMA_Y_FACTOR     24
#define TARGET_FRAME_TIME   25  // Desired update rate, though if too many leds it will just run as fast as it can!

uint32_t  LoopDelayMS = TARGET_FRAME_TIME;
uint32_t  LastLoop = millis() - LoopDelayMS;

void backgroundSwirl() {
  if ((millis() - LastLoop) >= LoopDelayMS) {
    LastLoop = millis();
    uint8_t blurAmount = beatsin8(2,10,255);
    blur2d( colorsBACK, WIDTH, HEIGHT, blurAmount);
   yield();
    // Use two out-of-sync sine waves
    uint8_t  i = beatsin8( 27, BORDER_WIDTH, HEIGHT-BORDER_WIDTH);
    uint8_t  j = beatsin8( 41, BORDER_WIDTH, WIDTH-BORDER_WIDTH);
    // Also calculate some reflections
    uint8_t ni = (WIDTH-1)-i;
    uint8_t nj = (WIDTH-1)-j;
   yield();
    // The color of each point shifts over time, each at a different speed.
    uint16_t ms = millis();  
    colorsBACK[XY( i, j)] += CHSV( ms / 11, 200, 255);
    colorsBACK[XY( j, i)] += CHSV( ms / 13, 200, 255);
    colorsBACK[XY(ni,nj)] += CHSV( ms / 17, 200, 255);
    colorsBACK[XY(nj,ni)] += CHSV( ms / 29, 200, 255);
    colorsBACK[XY( i,nj)] += CHSV( ms / 37, 200, 255);
    colorsBACK[XY(ni, j)] += CHSV( ms / 41, 200, 255);
  }
}

void backgroundPlasma2() {
   if ((millis() - LastLoop) >= LoopDelayMS) {
    LastLoop = millis();
   // Fill background with dim plasma
    for (int16_t y = 0; y < HEIGHT; y++) {
     for (int16_t x = 0; x < WIDTH; x++) {
        yield(); // secure time for the WiFi stack of ESP8266
        int16_t r = sin16(PlasmaTime) / 256;
        int16_t h = sin16(x * r * PLASMA_X_FACTOR + PlasmaTime) + 
                cos16(y * (-r) * PLASMA_Y_FACTOR + PlasmaTime) +
                sin16(y * x * (cos16(-PlasmaTime) / 256) / 2);
        colorsBACK[XY(x, y)] = CHSV((uint8_t)((h / 256) + 128), 200, 200);
      }
    }
    uint16_t OldPlasmaTime = PlasmaTime;
    PlasmaTime += PlasmaShift;
    if (OldPlasmaTime > PlasmaTime)
      PlasmaShift = (random8(0, 5) * 32) + 64;
  }
}

const int rows = HEIGHT;
const int cols = WIDTH;
/* Flare constants */
const uint8_t flarerows = 4;    /* number of rows (from bottom) allowed to flare */
const uint8_t maxflare = 2;     /* max number of simultaneous flares */
const uint8_t flarechance = 25; /* chance (%) of a new flare (if there's room) */
const uint8_t flaredecay = 18;  /* decay rate of flare radiation; 14 is good */

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
//unsigned long t = 0; /* keep time */
void backgroundFire() {
  uint16_t i, j;
//  if ( t > millis() ) return;
//  t = millis() + (1000 / FPS);

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
      colorsBACK[XY(j,rows - i - 1)] = colors[pix[i][j]];
    }
  }
 }
 

#if defined(USEFS)
// Functions to access a file on the SD card
fs::File myfile;
#endif

void* gifOpen(char *filename, int32_t *size) {
#if defined(USEFS) 
  myfile = FILESYSTEM.open(filename, "r");
  *size = myfile.size();
  return &myfile;
#else
   *size = 0;
   return 0;
#endif
}

void gifClose(void *handle) {
#if defined(USEFS) 
  if (myfile) myfile.close();
#endif  
}

int32_t gifRead(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen) {
#if defined(USEFS) 
    int32_t iBytesRead;
    iBytesRead = iLen;
    // Note: If you read a file all the way to the last byte, seek() stops working
    if ((pFile->iSize - pFile->iPos) < iLen)
       iBytesRead = pFile->iSize - pFile->iPos - 1; // <-- ugly work-around
    if (iBytesRead <= 0)
       return 0;
    iBytesRead = (int32_t)myfile.read(pBuf, iBytesRead);
    pFile->iPos = myfile.position();
    return iBytesRead;
 #else
  return 0;
 #endif
} 

int32_t gifSeek(GIFFILE *pFile, int32_t iPosition) { 
#if defined(USEFS) 
  myfile.seek(iPosition);
  pFile->iPos = (int32_t)myfile.position();
   return pFile->iPos;
#else
    return 0;
#endif
}

static void GIFDraw(int x, int y, int w, int h, uint16_t* lBuf) {
  // h == 1
  for (int idx = 0; idx < w; ++idx) {
    // convert uint16_t to 8 bit rgb
    uint16_t color565 = lBuf[idx];
    uint8_t r = ((((color565 >> 11) & 0x1F) * 527) + 23) >> 6;
    uint8_t g = ((((color565 >> 5) & 0x3F) * 259) + 33) >> 6;
    uint8_t b = (((color565 & 0x1F) * 527) + 23) >> 6;
    colorsBACK[XY(x + idx, y)] = CRGB(r, g, b);
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

boolean imageLoaded = false;

void loadNewImage(const char* url) {
  // create file from url
  if (WiFi.isConnected()) {
    WiFiClient wclient;
    HTTPClient client;
    if (client.begin(wclient, url)) {
      if (client.GET() > 0) {
        int len = client.getSize();
//        Serial.printf("size: %d\n", len);
        uint8_t buff[64] = { 0 }; 
#if defined(USEFS)      
        File file = FILESYSTEM.open("/image.gif", "w");
        if (!file) {
          Serial.println("There was an error opening the file for writing");
          return;
        } 
#endif
        while (client.connected() && (len > 0 || len == -1)) {
          // read up to 128 byte
          int c = wclient.readBytes(buff, std::min((size_t)len, sizeof(buff))); 
 //          Serial.printf("readBytes: %d\n", c);
          if (!c) {
 //           Serial.println("read timeout");
          }
#if defined(USEFS)
          // write it to File
          file.write(buff, c);
#endif
          if (len > 0) {
            len -= c;
          }  
        }
#if defined(USEFS)
        file.close();
#endif
      }  
    }
    client.end();
  }

  // setup GIF reader
  if (imageLoaded) 
    gif.close();
   imageLoaded = gif.open((char*)"/image.gif", gifOpen, gifClose, gifRead, gifSeek, GIFDraw);
}

void playGIFFrame() {
  if (imageLoaded) {
    gif.playFrame(true, NULL);
  } else {
    // no image loaded
    fill_solid(colorsBACK, NUM_LEDS, CRGB::HotPink);
  }
}

unsigned long backgroundMillis = 0;

void backgroundGIF() {
   if ((millis() - backgroundMillis) >= (animLength * 1000)) { // load new image
         backgroundMillis = millis();
         loadNewImage("http://merlinux.free.fr/gif32/gif2.php");
   }
   playGIFFrame();
}


void loop() {
   ArduinoOTA.handle();
   webServer.handleClient();
   switch (backgroundMode) {
    case 0: // blank
      display.clearDisplay(); 
      break;

     case 1: // plasma
        backgroundPlasma2();
        displayFastLED(colorsBACK);
        break;

      case 2: // fastled anim
         backgroundSwirl();
        displayFastLED(colorsBACK);
        break;

      case 3: // GIF
       backgroundGIF();
       displayFastLED(colorsBACK);
       break;

       case 4: // uniform color
       fill_solid(colorsBACK, NUM_LEDS, CRGB::HotPink);
        displayFastLED(colorsBACK);
        break;
        
        case 5: // fire
        backgroundFire();
         displayFastLED(colorsBACK);
        break;
        
        default:
         break;
      }
      yield();
   if (timeMode == 1) 
      displayTime();
    display.showBuffer();
   delay(1);
}

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
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
#include <FS.h>

#include <WiFiManager.h>

#include "image.h"

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

CRGB colorsTPM2[NUM_LEDS];
CRGB colorsBACK[NUM_LEDS];

Ticker display_ticker;

PxMATRIX display(WIDTH, HEIGHT, P_LAT, P_OE, P_A, P_B, P_C, P_D);
//PxMATRIX display(WIDTH, HEIGHT, P_LAT, P_OE, P_A, P_B, P_C, P_D, P_E);

uint8_t display_draw_time=50; //10-50 is usually fine
float isrDelay = 0.002;

void display_updater() {
      display.display(display_draw_time);
 }

#define TPM2NET_IN_PORT 65506
#define TPM2NET_OUT_PORT 65442

WiFiUDP udp;

const uint8_t packet_start_byte = 0x9c;
const uint8_t packet_type_data = 0xda;
const uint8_t packet_type_cmd = 0xc0;
const uint8_t packet_type_response = 0xaa;
const uint8_t packet_end_byte = 0x36;
const uint8_t packet_response_ack = 0xac;

// maximum udp esp8266 payload = 1460 -> 484 rgb leds per frame (22x22 matrix)
// then you have to use packet numbers and split the payload in 32x8 (4 packets)
const uint8_t rowsPerFrame = 8;
const uint16_t payload = WIDTH * rowsPerFrame * 3;
const uint16_t expected_packet_size = payload + 7;

const char* ACCESS_POINT = "wemos_p6_matrix";

void configModeCallback (WiFiManager *myWiFiManager) {
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

String fileList() {
      String str = "";
  Dir dir = SPIFFS.openDir("/");
  while (dir.next()) {
    str += dir.fileName();
    str += " -> ";
    str += dir.fileSize();
    str += " b\r\n";
  }
  return str;
}

void sendOk(String text = "Ok") {
  webServer.send(200, "text/plain", text);
}

void listFiles() {
  sendOk(fileList()); 
}

String logs;

void handleLog() {
  sendOk(logs); 
}

File fsUploadFile;

// curl -F "file=filename" <esp8266fs>/upload
void handleFileUpload() {
  if (webServer.uri() != "/upload") {
    return;
  }
  HTTPUpload& upload = webServer.upload();
  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    if (!filename.startsWith("/")) {
      filename = "/" + filename;
    }
//    Serial.print("handleFileUpload Name: "); Serial.println(filename);
    fsUploadFile = SPIFFS.open(filename, "w");
    filename.clear();
  } else if (upload.status == UPLOAD_FILE_WRITE) {
//    Serial.print("handleFileUpload Data: "); Serial.println(upload.currentSize);
    if (fsUploadFile) {
      fsUploadFile.write(upload.buf, upload.currentSize);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (fsUploadFile) {
      fsUploadFile.close();
    }
//    Serial.print("handleFileUpload Size: "); Serial.println(upload.totalSize);
  }
}


// image can be RGB or I
void drawImage(uint16_t xpos, uint16_t ypos, Image* image) {
  if (!image)
    return;
   for (uint16_t y = 0; y < image->getHeight(); ++y) {
    for (uint16_t x = 0; x < image->getWidth(); ++x) {
      CRGB& color = image->getPixel(x, y);
      display.drawPixelRGB888(xpos + x, ypos + y, color.r, color.g, color.b);
      yield();
    }
   }
}

Image* decodeFile(const char* name) {
   logString(String("Decoding file") + name);;
  File f = SPIFFS.open(name, "r");
  Image* image = 0;
  if (f) {
    logString(String("Decoding file") + name);;
      image = decodeBMPFile(f);
      f.close();
  } 
  return image;
}

boolean existsFile(const char* name) {
    File f = SPIFFS.open(name, "r");
    if (f) {
      f.close();
      return true;
    }
    return false;
}

int16_t read_int16(const uint8_t *data, unsigned int offset) {
        return (int16_t) (data[offset] | (data[offset + 1] << 8));
}

uint16_t read_uint16(const uint8_t *data, unsigned int offset) {
        return (uint16_t) (data[offset] | (data[offset + 1] << 8));
}

int32_t read_int32(const uint8_t *data, unsigned int offset) {
        return (int32_t) (data[offset] | (data[offset + 1] << 8) | 
                        (data[offset + 2] << 16) | (data[offset + 3] << 24));
}

uint32_t read_uint32(const uint8_t *data, uint32_t offset) {
        return (uint32_t) (data[offset] | (data[offset + 1] << 8) | 
                          (data[offset + 2] << 16) | (data[offset + 3] << 24));
}


#define BMP_FILE_HEADER_SIZE 14

typedef enum {
        BMP_ENCODING_RGB = 0,
        BMP_ENCODING_RLE8 = 1,
        BMP_ENCODING_RLE4 = 2,
        BMP_ENCODING_BITFIELDS = 3
} bmp_encoding;

typedef enum {
  RLE_EOL = 0,
  RLE_EOB = 1,
  RLE_DELTA = 2
} rle8;

#define TRUECOLOR 0

void logString(String thelog) {
#if 0
  logs += thelog;
  logs += "\n";
#else
//  Serial.println(thelog);
#endif
}

Image* decodeBMPFile(File file) {
  logString("Reading file");
    uint8_t header[BMP_FILE_HEADER_SIZE];
    uint8_t fourbytes[4];
   uint8_t r, g, b;
  if (file.size() <= BMP_FILE_HEADER_SIZE) {
      logString("File too small");
    return NULL;
  }
  file.readBytes((char*)header, BMP_FILE_HEADER_SIZE);
  if (header[0] != 'B' || header[1] != 'M') {
  logString("not a BMP Image");
    return NULL;
  }
  uint32_t offset = read_uint32(header, 10);
logString(String("offset ") + offset);
  // now we are at BITMAPINFOHEADER POS
  // read size from file
  file.readBytes((char*)fourbytes, 4);
  // rear from memory
  uint32_t biSize = read_uint32(fourbytes, 0);
 logString(String("biSize ") + biSize);
  // allocate data for BITMAPINFOHEADER and read
  uint8_t* infoheader = (uint8_t*)malloc(biSize - 4);
  file.readBytes((char*)infoheader, biSize - 4);

  // read normally from memory, starting from width
   int32_t biWidth = read_int32(infoheader, 0);
  logString(String("biWidth ") + biWidth);
     // negative means top down instead of bottom up
    int32_t biHeight = read_int32(infoheader, 4);
    logString(String("biHeight ") + biHeight);
    boolean reversed = false;
    if (biHeight < 0) {
      reversed = true;
      biHeight = -biHeight;
    }
    uint16_t biBitCount = read_uint16(infoheader, 10);
    logString(String("biBitCount ") + biBitCount);
   int32_t biCompression = read_int32(infoheader, 12);
    logString(String("biCompression ") + biCompression);
    uint32_t bytesPerRow = ((biWidth * biBitCount + 31) / 32) * 4;
   logString(String("bytesPerRow ") + bytesPerRow);
    // no need for BIH
    free(infoheader);
    uint8_t* palette = 0;
    uint16_t paletteSize = 0;
    if ((biBitCount == 8) || (biBitCount == 4)) { 
      paletteSize = (uint16_t)(1 << biBitCount); 
      logString(String("Palette size ") + paletteSize);
      // read palette from file
      palette = (uint8_t*)malloc(paletteSize * 4);
      // read normally from file
      file.readBytes((char*)palette, paletteSize * 4);
      // display palette RGBQUAD
      // char buffer[64];     
      //for (uint16_t idx = 0; idx < paletteSize; ++idx) {
      //  sprintf(buffer, "entry %d : %d %d %d", idx, palette[4 * idx + 2], palette[4 * idx + 1], palette[4 * idx + 0]);
       // Serial.println(buffer);
      //}
    } else if ((biBitCount == 24) || (biBitCount ==  32)) {
      // no palette
      logString(String("No palette"));
    } else { // not supported
     logString(String(biBitCount));
        return 0;
    }
#if TRUECOLOR
 Image* image = new Image(biWidth, biHeight);
#else
    Image* image = new Image(biWidth, biHeight, paletteSize);
    // copy palette to image
    if (palette != 0) {
      for (uint16_t c = 0; c < paletteSize; ++c) {
        uint8_t* paletteEntry = palette + 4 * c;                
        image->setCmap(c, CRGB(paletteEntry[2], paletteEntry[1], paletteEntry[0]));
      }
    }
#endif
    yield();
    // pixels follow at offset
    file.seek(offset, SeekSet);
     switch (biCompression) {
      case BMP_ENCODING_RGB:  { // supported case 32 24 and 8 bits/pixel
        // read data
 //       Serial.println("Reading pixel data");
            // allocating one line
        uint8_t* startRow = (uint8_t*)malloc(bytesPerRow);
        if (!startRow) {
          logString(String("Could not allocate ") + bytesPerRow );
        }
        for (int32_t y = 0; y < biHeight; ++y) {
          yield();
          logString(String("Line ") + y); 
          int32_t yy = reversed ? y : (biHeight - y - 1);
          file.readBytes((char*)startRow, bytesPerRow);
         // then parse data in memory
         if (biBitCount == 32) {
          for (int32_t x = 0; x < biWidth; ++x) {            
                r = startRow[x * 4 + 3]; g = startRow[x * 4 + 2]; b = startRow[x * 4 + 1];
               image->setPixel(x, yy, CRGB(r, g, b));
            }
         } else if (biBitCount == 24) { 
            for (int32_t x = 0; x < biWidth; ++x) {            
                r = startRow[x * 3 + 2]; g = startRow[x * 3 + 1]; b = startRow[x * 3 + 0];
               image->setPixel(x, yy, CRGB(r, g, b));
            }
          } else if (biBitCount == 8) {  // direct index to palette 
            for (int32_t x = 0; x < biWidth; ++x) { 
                  uint16_t index = startRow[x];
#if TRUECOLOR
                  uint8_t* paletteEntry = palette + 4 * index; 
                   r = paletteEntry[2]; g = paletteEntry[1]; b = paletteEntry[0];
                  image->setPixel(x, yy, CRGB(r, g, b)); 
 #else
                  image->setPixel(x, yy, index);    
 #endif
            }
          } else if (biBitCount == 4) {
            int32_t cpixel = 0;
            for (int32_t x = 0; x < biWidth; ++x) {
                uint8_t index1 = startRow[cpixel] >> 4;
                uint8_t index2 = startRow[cpixel] & 0xf;
#if TRUECOLOR
                uint8_t* paletteEntry1 = palette + 4 * index1;
                r = paletteEntry1[2]; g = paletteEntry1[1]; b = paletteEntry1[0];
                // emit first pixel
                image->setPixel(x, yy, CRGB(r, g, b));
#else
                image->setPixel(x, yy, index1);
 #endif
                ++x;
                if (x < biWidth) {
                  // emit the second pixel
#if TRUECOLOR                  
                  uint8_t* paletteEntry2 = palette + 4 * index2;
                  r = paletteEntry2[2]; g = paletteEntry2[1]; b = paletteEntry2[0];
                  image->setPixel(x, yy, CRGB(r, g, b));
#else
                  image->setPixel(x, yy, index2);
#endif                  
                }
                cpixel++;
             }
           }
        }
       free(startRow);
         break;
      }
      case BMP_ENCODING_RLE8: {
          logString("Reading RLE pixel data");
           int32_t x = 0; int32_t y = 0;
           boolean decode = true;
           while (decode == true) {
            yield();
                uint8_t length;
                file.readBytes((char*)&length, 1);
                if (length == 0) {
                  uint8_t code;
                  file.readBytes((char*)&code, 1);
                  switch (code) {
                    case RLE_EOL: { // end of line
                      x = 0; y++;
                      break;
                    }
                    case RLE_EOB: { // end of file
                      decode = false;
                      break;
                    }
                    case RLE_DELTA: { // offset mode
                      uint8_t delta[2];
                      file.readBytes((char*)delta, 2);
                      x += delta[0]; y += delta[1];
                      break;
                    } 
                    default: { // literal mode
                      file.readBytes((char*)&length, 1);
                      for (uint8_t idx = 0; idx < length; ++idx) {
                        if (x >= biWidth) {
                          x = 0; y++;
                        }
                        // use literal value
                        uint8_t index;
                        file.readBytes((char*)&index, 1);
                        int32_t yy = reversed ? y : (biHeight - y - 1);
#if TRUECOLOR                        
                        const uint8_t* paletteEntry = palette + 4 * index; 
                        r = paletteEntry[2]; g = paletteEntry[1]; b = paletteEntry[0];
                        image->setPixel(x, yy, CRGB(r, g, b));
#else
                        image->setPixel(x, yy, index);
#endif                                                
                        x++;
                      }
                      if (length & 1) // fill byte
                        file.readBytes((char*)index, 1); // not used
                    }
                  }
              } else { // normal run
                  uint8_t index;
                  file.readBytes((char*)&index, 1);
                  for (uint8_t idx = 0; idx < length; ++idx) {
                    yield();
                      if (x >= biWidth) {
                        x = 0; y++;
                      }
                      int32_t yy = reversed ? y : (biHeight - y - 1);
#if TRUECOLOR                     
                      const uint8_t* paletteEntry = palette + 4 * index; 
                      r = paletteEntry[2]; g = paletteEntry[1]; b = paletteEntry[0];
                      image->setPixel(x, yy, CRGB(r, g, b));
#else
                      image->setPixel(x, yy, index);
#endif
                    x++;
                  } 
                  if (y >= biHeight) {
                      decode = false;         
                  }
              } 
           }      
        break;
      }
    } 
    free(palette);
    return image;
 }

unsigned long lastFrameTime = 0;

int backgroundMode = 1; // plasma
int timeMode = 1;

uint16_t myWHITE = display.color565(255, 255, 255);
uint16_t myBLACK = display.color565(0, 0, 0);
uint16_t myGREY = display.color565(32, 32, 32);

Image* background;

void setup() {
// Serial.begin(115200);
  SPIFFS.begin();
  display.begin(16);
  display.flushDisplay();
 display.setFastUpdate(true);
   display.setBrightness(255);
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
    udp.begin(TPM2NET_IN_PORT);
    udpNTP.begin(NTP_PORT);
    initOTA();

   webServer.on("/intensity", HTTP_GET, []() {
      String value = webServer.arg("value");
      display.setBrightness(value.toInt());
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
    // curl -F 'data=@background00.bmp' http://192.168.0.45/upload
    // for i in background??.bmp; do curl -F data=@$i http://192.168.0.45/upload; done
    webServer.on("/upload", HTTP_POST, []() { sendOk(); }, handleFileUpload);
    webServer.on("/list", HTTP_GET, []() { listFiles(); });  
    webServer.on("/log", HTTP_GET, []() { handleLog(); }); 
    webServer.on("/format", HTTP_GET, []() { boolean ret = SPIFFS.format(); sendOk(ret ? "Success" : "Failure"); });
    webServer.begin();
    display.setCursor(0, 0);
    display.setTextColor(myWHITE);
    display.print(WiFi.localIP());
    display.showBuffer();
  setSyncInterval(3600);
   setSyncProvider(getNtpTime);
    delay(4000);
  }
    // background = decodeFile("/background00.bmp");
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
  display.setTextColor(myGREY);
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

const void sendAckUDP() {
  udp.beginPacket(udp.remoteIP(), TPM2NET_OUT_PORT);
  udp.write(&packet_response_ack, 1);
  udp.endPacket();
}

boolean firstFrame = true;
boolean completeFrame = false;

void loopUDP() {
  uint16_t packet_size = udp.parsePacket();
  if (packet_size == 0)
    return;
  // then parse to check
  if (udp.read() != packet_start_byte)
    return;
  // packet type
  uint8_t ptype = udp.read();
  uint16_t frame_size = (udp.read() << 8) | udp.read();
  uint8_t packet = udp.read() - 1; //  starts at 1
  uint8_t npackets = udp.read();
//  Serial.println(String("frameSize: ") + frame_size + " packet: " + packet + "/" + npackets);

  switch (ptype) {
    case packet_type_response:
//      Serial.println("Response");
      sendAckUDP();
      break;

    case packet_type_data: {
        // reunite splitted packets
        if (frame_size != payload)
          return;
          firstFrame = true;
          lastFrameTime = millis();
//        Serial.println("Data");
        // fills the matrix directly, starting at packet number offset
        uint16_t offset = payload * packet;
        udp.read((char*)(colorsTPM2 + offset / 3), payload);
        if (packet == (npackets - 1))
           completeFrame = true;
         else
         completeFrame = false;
        break;
      }
    case packet_type_cmd: {
        break;
      }
  }
  // skip end byte, maybe not neccessary 0x36
  udp.read();
}


uint16_t PlasmaTime = 0;
uint16_t PlasmaShift = (random8(0, 5) * 32) + 64;

#define PLASMA_X_FACTOR     24
#define PLASMA_Y_FACTOR     24
#define TARGET_FRAME_TIME   25  // Desired update rate, though if too many leds it will just run as fast as it can!

uint32_t  LoopDelayMS = TARGET_FRAME_TIME;
uint32_t  LastLoop = millis() - LoopDelayMS;

void backgroundSwirl() {
  if (abs(millis() - LastLoop) >= LoopDelayMS) {
    LastLoop = millis();
    uint8_t blurAmount = beatsin8(2,10,255);
    blur2d( colorsBACK, WIDTH, HEIGHT, blurAmount);
  
    // Use two out-of-sync sine waves
    uint8_t  i = beatsin8( 27, BORDER_WIDTH, HEIGHT-BORDER_WIDTH);
    uint8_t  j = beatsin8( 41, BORDER_WIDTH, WIDTH-BORDER_WIDTH);
    // Also calculate some reflections
    uint8_t ni = (WIDTH-1)-i;
    uint8_t nj = (WIDTH-1)-j;
  
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
   if (abs(millis() - LastLoop) >= LoopDelayMS) {
    LastLoop = millis();
   // Fill background with dim plasma
    for (int16_t y = 0; y < WIDTH; y++) {
     for (int16_t x = 0; x < WIDTH; x++) {
        yield(); // secure time for the WiFi stack of ESP8266
        int16_t r = sin16(PlasmaTime) / 256;
        int16_t h = sin16(x * r * PLASMA_X_FACTOR + PlasmaTime) + cos16(y * (-r) * PLASMA_Y_FACTOR + PlasmaTime) + sin16(y * x * (cos16(-PlasmaTime) / 256) / 2);
        colorsBACK[XY(x, y)] = CHSV((uint8_t)((h / 256) + 128), 255, 255);
      }
    }
    uint16_t OldPlasmaTime = PlasmaTime;
    PlasmaTime += PlasmaShift;
    if (OldPlasmaTime > PlasmaTime)
      PlasmaShift = (random8(0, 5) * 32) + 64;
  }
}

unsigned long backgroundMillis = 0;
const float backgroundFPS = 10;
int currentFrame = 0;
const unsigned long animLength = (unsigned long)(1000.0 / backgroundFPS);
 
void loop() {
   ArduinoOTA.handle();
   webServer.handleClient();
 loopUDP();
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

      case 3: // BMP 
          if ((millis() - backgroundMillis) >= animLength) { // switch images
             backgroundMillis = millis();
             char buffer[16];
              sprintf(buffer, "/background%.2d.bmp", currentFrame);
              if (!existsFile(buffer)) {
                currentFrame = 0;
                sprintf(buffer, "/background%.2d.bmp", currentFrame);
              }
              delete background;
              background = decodeFile(buffer);
              currentFrame += 1;
          } 
          drawImage(0, 0, background);
          break;

      case 4: // TPM2
          loopUDP();
          if (completeFrame)
            displayFastLED(colorsTPM2);
          break;

       default:
         break;
      }
      yield();
   if (timeMode == 1) 
      displayTime();
    display.showBuffer();
    delay(20);
}

import hypermedia.net.*;
import java.awt.Rectangle;
import java.net.*;  

UDP udp;  // define the UDP object

int width_matrix = 32;
int height_matrix = 32;

int border = 1; 
int pixelWidth = 15;
int pixelHeight = pixelWidth;

Rectangle[][] rectangles = new Rectangle[width_matrix][height_matrix];
color[][] colors = new color[width_matrix][height_matrix];

void settings() {
 size(width_matrix * pixelWidth + (width_matrix + 1) * border, 
       height_matrix * pixelHeight + (height_matrix + 1) * border);
}  
void setup() {
//  frameRate(30);
  noLoop();
  try {
  InetAddress IP=InetAddress.getLocalHost();
  System.out.println(IP.toString());
  } catch (Exception e){
    System.out.println(e);
  }  
  
  udp = new UDP( this, 65506 );
  udp.listen( true );
  
    // create drawing area
  int ypos = border;
  int xpos;
  for (int y = 0; y < height_matrix; ++y) {
    xpos = border;
    for (int x = 0; x < width_matrix; ++x) {
      rectangles[x][y] = new Rectangle(xpos, ypos, pixelWidth, pixelHeight);
      xpos += border + pixelWidth;
    }
    ypos += border + pixelHeight; 
  }
  // init colors
    for (int y = 0; y < height_matrix; ++y) {
      for (int x = 0; x < width_matrix; ++x) {
        colors[x][y] = color(0, 0, 0);
      }
    }
}

void draw() {
  noStroke();
  fill(color(12, 12, 12));
  rect(0, 0, width, height);
  for (int y = 0; y < height_matrix; ++y) {
    for (int x = 0; x < width_matrix; ++x) {
      fill(colors[x][y]);
      Rectangle rectangle = rectangles[x][y];
      rect((float)rectangle.getX(), (float)rectangle.getY(),
        (float)rectangle.getWidth(), (float)rectangle.getHeight());
    }
  } 
}

void receive( byte[] data) { 
//  println( "receive: " + data.length );
// process header
   byte startByte = data[0];
   if (startByte != (byte)0x9c)
     return;

   byte packetType = data[1];
//   int frameSize = (data[2] & 0xff) * 256 + (data[3] & 0xff);
   int packetNumber = data[4] - 1;
//   int numPackets = data[5];
 //     println( "frame size: " + frameSize + " packet: " + packetNumber + "/" + numPackets);
//   if (packetNumber != 3) return;
   if (packetType == (byte)0xda) {
     int ystart = 8 * packetNumber;
     int offset = 6;
     for (int y = 0; y < 8; ++y) {
       for (int x = 0; x < width_matrix; ++x) {
         colors[x][y + ystart] = color(data[offset + 0] & 0xff, data[offset + 1] & 0xff, data[offset + 2] & 0xff);
         offset += 3;
       }
     }
     if (packetNumber == 3) redraw();
   }
}

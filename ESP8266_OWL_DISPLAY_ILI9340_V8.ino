// (C) D Bird 2016
// Network Owl power consumption decoder and display for Arduino/ESP8266 and a 2.2" ADAFruit TFT Display
/*
Processor pin ---> Display Pin
------------------------------
ESP8266 3.3V  ---> TFT Reset  (Reset - needs to be high, or device is reset, this is really NOT RESET meaning a low resets the device)
ESP8266 3.3V  ---> TFT Vin    (Supply)
ESP8266 Gnd   ---> TFT Gnd    (Ground)
ESP8266 D8    ---> TFT LCD CS (TFT/Display chip select)
ESP8266 D7    ---> TFT MOSI   (Master Out Slave In, used to send commands and data to TFT and SD Card
ESP8266 D6    ---> TFT MISO   (Master In Slave Out, used to get data from SD Card)
ESP8266 D5    ---> TFT SCK    (Clock)
ESP8266 D4    ---> TFT D/C    (Data or Command selector)
ESP8266 D3    ---> TFT SD CS  (SD Card chip select)
ESP8266 D2    ---> not used
ESP8266 D1    ---> not used
ESP8266 D0    ---> TFT Backlight (Used to strobe the LED backlight and reduce power consumption, can be set High permanently too, 3.3V would do)
*/

#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <WiFiUdp.h>
#include <WiFiClient.h>
#include <SPI.h>
#include <SD.h>
#include "Adafruit_GFX.h"
#include "Adafruit_ILI9341.h"
#include <EEPROM.h> 
#include <time.h>

//----------------------------------------------------------------------------------------------------
#// For the Adafruit shield, these are the default.
#define SD_CS  D3     // Chip Select for SD-Card
#define TFT_DC D4     // Data/Command pin for SPI TFT screen
#define TFT_CS D8     // Chip select for TFT screen

// Use hardware SPI (on Uno, #13, #12, #11) and the above for CS/DC
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC);

#define BUFFPIXEL 20     // Used for BMP file display
#define xpos 220         // X Position of icon 0,0 is screen top left
#define ypos 0           // Y Position of icon 
#define graph_scale 5000 // Maximum value of data to be displayed
#define graph_height 110 // Height of graph in pixels

// Assign names to common 16-bit color values:
#define BLACK       0x0000
#define BLUE        0x001F
#define RED         0xF800
#define GREEN       0x07E0
#define CYAN        0x07FF
#define YELLOW      0xFFE0
#define WHITE       0xFFFF

char ssid[] = "Your SSID here"; // your network SSID (name)
char pass[] = "Your Password here";     // your network password

IPAddress ipMulticast(224, 192, 32, 19);  // Set the IP Multicast IP address to be monitored
unsigned int localPort = 22600;           // Set local port to listen for UDP packets on, Network Owl uses 22600

// An EthernetUDP instance to let us send and receive packets over UDP
WiFiUDP    Udp;
WiFiClient client;

#define buffer_size 355 // Buffer to hold incoming packet data
#define max_readings 100

char    packetBuffer[buffer_size]; 
int     UDP_Msg_start_point, packetSize;
boolean address_found = true;
int     rx_count = 1;
String  UDP_Data;
int     temperature,  wx_code;
String  weather_conditions = "Displaying last weather";
String  weather_conditions_rx;
int     reading = 1;
int     power_reading[max_readings+1]    = {0};
float   power_cumulative[max_readings+1] = {0};
float   Max_watts, Min_watts, KW_Hours, Watts, Last_Watts, Last_KW_Hours  = 0;

//----------------------------------------------------------------------------------------------------
// These read 16 and 32-bit data types from the SD card file.
// BMP data is stored little-endian, ESP8266 or Arduino are little-endian too.

uint16_t read16(File & f) {
  uint16_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read(); // MSB
  return result;
}

//----------------------------------------------------------------------------------------------------
uint32_t read32(File & f) {
  uint32_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read();
  ((uint8_t *)&result)[2] = f.read();
  ((uint8_t *)&result)[3] = f.read(); // MSB
  return result;
}

//----------------------------------------------------------------------------------------------------
void BmpDraw( const char*filename, uint16_t x, uint16_t y) {
  File     bmpFile;
  int      bmpWidth, bmpHeight;   // W+H in pixels
  uint8_t  bmpDepth;              // Bit depth (currently must be 24)
  uint32_t bmpImageoffset;        // Start of image data in file
  uint32_t rowSize;               // Not always = bmpWidth; may have padding
  uint8_t  sdbuffer[3*BUFFPIXEL]; // pixel buffer (R+G+B per pixel)
  uint8_t  buffidx = sizeof(sdbuffer); // Current position in sdbuffer
  boolean  goodBmp = false;       // Set to true on valid header parse
  boolean  flip    = true;        // BMP is stored bottom-to-top
  int      w, h, row, col;
  uint8_t  r, g, b;
  uint32_t pos = 0, startTime = millis();

  Serial.println();
  Serial.print("Loading image '");
  Serial.print(filename);
  Serial.println('\'');

  // Open requested file on SD card
  if ((bmpFile = SD.open(filename)) == 0) {
    Serial.print("File not found");
    return;
  }
  // Parse BMP header
  if(read16(bmpFile) == 0x4D42) { // BMP signature
    Serial.print("File size: "); Serial.println(read32(bmpFile));
    read32(bmpFile); // Read & ignore creator bytes
    bmpImageoffset = read32(bmpFile); // Start of image data
    Serial.print("Image Offset: "); Serial.println(bmpImageoffset, DEC);
    // Read DIB header
    Serial.print("Header size: "); Serial.println(read32(bmpFile));
    bmpWidth  = read32(bmpFile);
    bmpHeight = read32(bmpFile);
    if(read16(bmpFile) == 1) { // # planes -- must be '1'
      bmpDepth = read16(bmpFile); // bits per pixel
      Serial.print("Bit Depth: "); Serial.println(bmpDepth);
      if((bmpDepth == 24) && (read32(bmpFile) == 0)) { // 0 = uncompressed
        goodBmp = true; // Supported BMP format -- proceed!
        Serial.print("Image size: ");
        Serial.print(bmpWidth);
        Serial.print('x');
        Serial.println(bmpHeight);
        // BMP rows are padded (if needed) to 4-byte boundary
        rowSize = (bmpWidth * 3 + 3) & ~3;
        // If bmpHeight is negative, image is in top-down order.
        if(bmpHeight < 0) {
          bmpHeight = -bmpHeight;
          flip      = false;
        }
        // Crop area to be loaded
        w = bmpWidth;
        h = bmpHeight;
        if((x+w-1) >= tft.width())  { w = tft.width()  - x; }
        if((y+h-1) >= tft.height()) { h = tft.height() - y; }

        // Set TFT address window to clipped image bounds
        tft.setAddrWindow(x, y, x+w-1, y+h-1);

        for (row=0; row<h; row++) { // For each scanline...
          // Seek to start of scan line.  It might seem labor-
          // intensive to be doing this on every line, but this
          // method covers a lot of gritty details like cropping
          // and scanline padding.  Also, the seek only takes
          // place if the file position actually needs to change
          // (avoids a lot of cluster math in SD library).
          if(flip) // Bitmap is stored bottom-to-top order (normal BMP)
            { pos = bmpImageoffset + (bmpHeight - 1 - row) * rowSize; }
          else {    // Bitmap is stored top-to-bottom
            pos = bmpImageoffset + row * rowSize;
          }
          if(bmpFile.position() != pos) { // Need seek?
            bmpFile.seek(pos);
            buffidx = sizeof(sdbuffer); // Force buffer reload
          }

          for (col=0; col<w; col++) { // For each pixel...
            // Time to read more pixel data?
            if (buffidx >= sizeof(sdbuffer)) { // Indeed
              bmpFile.read(sdbuffer, sizeof(sdbuffer));
              buffidx = 0; // Set index to beginning
            }
            // Convert pixel from BMP to TFT format, push to display
            b = sdbuffer[buffidx++];
            g = sdbuffer[buffidx++];
            r = sdbuffer[buffidx++];
            int rgb = ((r / 8) << 11) | ((g / 4) << 5) | (b / 8); // Convert RGB data to 5-6-5 bit data into a 16-bit byte
            tft.pushColor(rgb);
          } // end pixel
        } // end scanline
        Serial.print("Loaded in "); Serial.print(millis() - startTime); Serial.println(" ms");
      } // end Bmp Display
    }
  }
  bmpFile.close();
  if(!goodBmp) Serial.println("BMP format not recognized.");
}
//----------------------------------------------------------------------------------------------------
void DisplayWxIcon(int wx_code, int x, int y) {
   if (wx_code == 113)  { weather_conditions = "Clear/Sunny";                                 BmpDraw("w1.bmp" ,x,y); } // Sunny
   if (wx_code == 116)  { weather_conditions = "Cloudy/Sunny Intervals";                      BmpDraw("w3.bmp" ,x,y); } // Sunny intervals
   if (wx_code == 119)  { weather_conditions = "Cloudy";                                      BmpDraw("w6.bmp" ,x,y); } // Cloudy
   if (wx_code == 122)  { weather_conditions = "Overcast";                                    BmpDraw("w7.bmp" ,x,y); } // Overcast
   if (wx_code == 143)  { weather_conditions = "Mist";                                        BmpDraw("w4.bmp" ,x,y); } // Mist
   if (wx_code == 176)  { weather_conditions = "Patchy rain nearby";                          BmpDraw("w9.bmp" ,x,y); } // Patchy rain nearby
   if (wx_code == 179)  { weather_conditions = "Patchy snow nearby";                          BmpDraw("w22.bmp",x,y); } // Patchy snow nearby
   if (wx_code == 182)  { weather_conditions = "Patchy sleet nearby";                         BmpDraw("w16.bmp",x,y); } // Patchy sleet nearby
   if (wx_code == 185)  { weather_conditions = "Patchy freezing drizzle nearby";              BmpDraw("w17.bmp",x,y); } // Patchy freezing drizzle nearby
   if (wx_code == 200)  { weather_conditions = "Thundery outbreaks";                          BmpDraw("w29.bmp",x,y); } // Thundery outbreaks
   if (wx_code == 227)  { weather_conditions = "Blowing snow";                                BmpDraw("w25.bmp",x,y); } // Blowing snow
   if (wx_code == 230)  { weather_conditions = "Blizzard";                                    BmpDraw("w25.bmp",x,y); } // Blizzard
   if (wx_code == 248)  { weather_conditions = "Fog";                                         BmpDraw("w6.bmp", x,y); } // Fog
   if (wx_code == 260)  { weather_conditions = "Freezing fog";                                BmpDraw("w5.bmp", x,y); } // Freezing fog
   if (wx_code == 263)  { weather_conditions = "Patchy light drizzle";                        BmpDraw("w10.bmp",x,y); } // Patchy light drizzle
   if (wx_code == 266)  { weather_conditions = "Light drizzle";                               BmpDraw("w11.bmp",x,y); } // Light drizzle
   if (wx_code == 281)  { weather_conditions = "Freezing drizzle";                            BmpDraw("w16.bmp",x,y); } // Freezing drizzle
   if (wx_code == 284)  { weather_conditions = "Heavy freezing drizzle";                      BmpDraw("w22.bmp",x,y); } // Heavy freezing drizzle
   if (wx_code == 293)  { weather_conditions = "Patchy light rain";                           BmpDraw("w9.bmp" ,x,y); } // Patchy light rain
   if (wx_code == 296)  { weather_conditions = "Light rain";                                  BmpDraw("w11.bmp",x,y); } // Light rain
   if (wx_code == 299)  { weather_conditions = "Moderate rain at times";                      BmpDraw("w13.bmp",x,y); } // Moderate rain at times
   if (wx_code == 302)  { weather_conditions = "Moderate rain";                               BmpDraw("w14.bmp",x,y); } // Moderate rain
   if (wx_code == 305)  { weather_conditions = "Heavy rain at times";                         BmpDraw("w14.bmp",x,y); } // Heavy rain at times
   if (wx_code == 308)  { weather_conditions = "Heavy rain";                                  BmpDraw("w14.bmp",x,y); } // Heavy rain
   if (wx_code == 311)  { weather_conditions = "Light freezing rain";                         BmpDraw("w16.bmp",x,y); } // Light freezing rain
   if (wx_code == 314)  { weather_conditions = "Moderate or heavy freezing rain";             BmpDraw("w22.bmp",x,y); } // Moderate or heavy freezing rain
   if (wx_code == 317)  { weather_conditions = "Light sleet";                                 BmpDraw("w17.bmp",x,y); } // Light sleet
   if (wx_code == 320)  { weather_conditions = "Moderate or heavy sleet";                     BmpDraw("w16.bmp",x,y); } // Moderate or heavy sleet
   if (wx_code == 323)  { weather_conditions = "Patchy light snow";                           BmpDraw("w22.bmp",x,y); } // Patchy light snow
   if (wx_code == 326)  { weather_conditions = "Light snow";                                  BmpDraw("w23.bmp",x,y); } // Light snow
   if (wx_code == 329)  { weather_conditions = "Patchy moderate snow";                        BmpDraw("w23.bmp",x,y); } // Patchy moderate snow
   if (wx_code == 332)  { weather_conditions = "Moderate snow";                               BmpDraw("w25.bmp",x,y); } // Moderate snow
   if (wx_code == 335)  { weather_conditions = "Patchy heavy snow";                           BmpDraw("w26.bmp",x,y); } // Patchy heavy snow
   if (wx_code == 338)  { weather_conditions = "Heavy snow";                                  BmpDraw("w26.bmp",x,y); } // Heavy snow
   if (wx_code == 350)  { weather_conditions = "Ice pellets";                                 BmpDraw("w20.bmp",x,y); } // Ice pellets
   if (wx_code == 353)  { weather_conditions = "Light rain shower";                           BmpDraw("w11.bmp",x,y); } // Light rain showers
   if (wx_code == 356)  { weather_conditions = "Moderate or heavy rain showers";              BmpDraw("w14.bmp",x,y); } // Moderate or heavy rain showers
   if (wx_code == 359)  { weather_conditions = "Torrential rain shower";                      BmpDraw("w14.bmp",x,y); } // Torrential rain shower
   if (wx_code == 362)  { weather_conditions = "Light sleet showers";                         BmpDraw("w17.bmp",x,y); } // Light sleet showers
   if (wx_code == 365)  { weather_conditions = "Moderate or heavy sleet showers";             BmpDraw("w17.bmp",x,y); } // Moderate or heavy sleet showers
   if (wx_code == 368)  { weather_conditions = "Light snow showers";                          BmpDraw("w21.bmp",x,y); } // Light snow showers
   if (wx_code == 371)  { weather_conditions = "Moderate or heavy snow showers";              BmpDraw("w25.bmp",x,y); } // Moderate or heavy snow showers
   if (wx_code == 374)  { weather_conditions = "Moderate or heavy showers of ice pellets";    BmpDraw("w20.bmp",x,y); } // Moderate or heavy showers of ice pellets
   if (wx_code == 377)  { weather_conditions = "Light shower of ice pellets";                 BmpDraw("w20.bmp",x,y); } // Light shower of ice pellets
   if (wx_code == 386)  { weather_conditions = "Patchy light rain in area with thunder";      BmpDraw("w28.bmp",x,y); } // Patchy light rain in area with thunder
   if (wx_code == 389)  { weather_conditions = "Moderate or heavy rain in area with thunder"; BmpDraw("w29.bmp",x,y); } // Moderate or heavy rain in area with thunder
   if (wx_code == 392)  { weather_conditions = "Patchy light snow in area with thunder";      BmpDraw("w28.bmp",x,y); } // Patchy light snow in area with thunder
   if (wx_code == 395)  { weather_conditions = "Moderate or heavy snow in area with thunder"; BmpDraw("w28.bmp",x,y); } // Moderate or heavy snow in area with thunder
   if (wx_code == 000)  { weather_conditions = "No data received";                            BmpDraw("w99.bmp",x,y); } // No data received
}
 /* (C) D L BIRD
 *  This function will draw a graph on a TFT / LCD display, it requires two arrays, in this usage one for power readings and for cumulative power.
 *  The variable max_readings determines the maximum number of data elements for each array. Call it with the following parametric data:
 *  x_pos - the x axis top-left position of the graph
 *  y_pos - the y-axis top-left position of the graph, e.g. 100, 200 would draw the graph 100 pixels along and 200 pixels down from the top-left of the screen
 *  width - the width of the graph in pixels
 *  height - height of the graph in pixels
 *  Y1_Max - sets the scale of plotted data, for example 5000 would scale all data to a Y-axis of 5000 maximum
 *  data_array1 and data_array2 are arrays parsed by value, externally they can be called anything else, e.g. within the routine one is called data_array1, but extgernally can be temperature_readings
 *  auto_scale - a logical calue (TRUE or FALSE) that switches the Y-axis autoscale On or Off.
 *  If the data never goes above 500 then if on, the Y-axis scale will be 0-500, if off it will be scalled up to 5000 in 500 steps, determined by the definition of auto_scale_major_tick, set to 1000 and autoscale with increment the sacale in 1000 steps.
 */
 void DrawGraph(int x_pos, int y_pos, int width, int height, int Y1_Max, int data_array1[max_readings], float data_array2[max_readings], boolean auto_scale) {
  #define auto_scale_major_tick 500
  int Y2_Max = 15;
  if (auto_scale) {
    int max_Y_scale = 0;
    for (int i=1; i <= max_readings; i++ ){ //  Find maximum data value to be displayed
      if (data_array1[i] > max_Y_scale) max_Y_scale = data_array1[i];
    }
    Y1_Max =  ((max_Y_scale + auto_scale_major_tick - 1) / auto_scale_major_tick) * auto_scale_major_tick; // Auto scale the graph and round to the nearest value defined, default was 500
  }
  //Graph the received data contained in an array
  #define yticks 5     // 5 y-axis division markers
  // Draw the graph outline
  tft.drawRect(x_pos,y_pos,width+3,height+2,WHITE);
  int x1,y1,x2,y2;
  for(int gx = 1; gx <= max_readings; gx++){
    x1 = x_pos + gx * width/max_readings; 
    y1 = y_pos + height;
    x2 = x_pos + gx * width/max_readings; // max_readings is the global variable that sets the maximum data that can be plotted 
    y2 = y_pos + height - constrain(data_array1[gx],0,Y1_Max) * height / Y1_Max + 1; 
    tft.drawLine(x1,y1,x2,y2,RED);
    y1 = y_pos + height - constrain(data_array2[gx],0,Y2_Max) * height / Y2_Max + 1; // 15 is the maximum on Y2 (right-hand axis)
    tft.drawPixel(x1,y1,CYAN);
    tft.drawPixel(x1,y1-1,CYAN);
  }
  //Draw the left and right Y-axis scales
  for (int spacing = 0; spacing <= yticks; spacing++) {
    for (int j=0; j <20; j++){ // Draw dashed graph grid lines
      if (spacing < yticks) tft.drawFastHLine((x_pos+1+j*width/20),y_pos+(height*spacing/yticks),width/40,WHITE);
    }
    //Left-hand axis
    tft.setTextColor(YELLOW);
    tft.setCursor((x_pos-26),(y_pos-4)+height*spacing/yticks);
    tft.print(Y1_Max - Y1_Max/yticks*spacing);
    //Right-hand axis
    tft.setTextColor(CYAN);
    tft.setCursor((x_pos+width+yticks),y_pos+height*spacing/yticks-4);
    tft.print((yticks-spacing)*3);
  }
}
//----------------------------------------------------------------------------------------------------

void setup(){
  Serial.begin(115200);
  WiFi.begin(ssid, pass);
  configTime(1 * 3600, 0, "pool.ntp.org", "time.nist.gov");
    
  EEPROM.begin(32); // Sufficient storage for two integer values 
  delay(200);

  // Include these lines for the first run to set the EEPROM contents, or wait until they are broadcast!
  //EEPROM.put(0,16);  // default value set to '16'-deg C
  //EEPROM.put(4,116); // default value set to 'Cloudy/Sunny Intervals'
  //EEPROM.commit();
  
  EEPROM.get(0,temperature); // EEPROM.get(address,variable_of_type)
  EEPROM.get(4,wx_code);
  
  Udp.beginMulticast(WiFi.localIP(), ipMulticast, localPort);

  analogWriteFreq(500); // Enable TFT display brightness
  analogWrite(D0, 750); // Set display brightness using D0 as driver

  tft.begin();
  tft.setRotation(3);
  tft.setTextSize(2);
  tft.fillScreen(BLACK);
  if (!SD.begin(SD_CS)) {
    return;
  }
  Max_watts = 0;      // Set to record maximum watts
  Min_watts = 10000;  // Set to record minimum watts
  delay(1000);
  // Display a picture to see while we wait for data to arrive
  tft.println("   Waiting for UDP data");
  Serial.println("Drawing opening image");
  BmpDraw("bertie.bmp", 75, 20);
}

// ---------------------------------------------------

void loop(){
  address_found = true;
  packetSize = Udp.parsePacket();
  if(packetSize) {     
    if (address_found) {
      IPAddress remote = Udp.remoteIP();
      address_found = false;
    }
    Udp.read(packetBuffer,buffer_size);
        Serial.println("Received data:");
    UDP_Data = packetBuffer; // Convert char array to string for ease of manipulation
    Serial.println(UDP_Data); // Diagnostic print. If required uncomment start of line
    Serial.print("Data packet size ");
    Serial.println(packetSize);
    // First look for Watts data
    Last_Watts = Watts;      // Save last Watts value
    UDP_Msg_start_point = UDP_Data.indexOf("<curr units=\'w\'>") + 16;
    Watts = UDP_Data.substring(UDP_Msg_start_point,UDP_Data.indexOf("</curr>",UDP_Msg_start_point)).toFloat();
    if (Watts > Max_watts) Max_watts = Watts;
    if (Watts < Min_watts) Min_watts = Watts;
    power_reading[reading] = Watts;
 
    // Next look for accumlated Watts used today
    UDP_Msg_start_point = UDP_Data.indexOf("<day units=\'wh\'>") + 16;
    Last_KW_Hours = KW_Hours;
    KW_Hours = (UDP_Data.substring(UDP_Msg_start_point,UDP_Data.indexOf("</day>",UDP_Msg_start_point))).toFloat() / 1000;
    if (KW_Hours == 0) {KW_Hours = Last_KW_Hours;} // Removes errorenous data reception
    power_cumulative[reading] = KW_Hours;

    // Display associated weather data e.g. "<weather id='00A0C914C851' code='263'><temperature>19.00</temperature><text>Patchy light drizzle</text></weather>"
    if (UDP_Data.indexOf("<weather") >= 0) {
      UDP_Msg_start_point = UDP_Data.indexOf("<temperature>") + 13;
      temperature = UDP_Data.substring(UDP_Msg_start_point,UDP_Data.indexOf("</temperature>",UDP_Msg_start_point)-3).toInt();
      UDP_Msg_start_point   = UDP_Data.indexOf("<text>") + 6;
      weather_conditions_rx = UDP_Data.substring(UDP_Msg_start_point,UDP_Data.indexOf("</text>",UDP_Msg_start_point));
      // Extract weather code e.g. <weather id='00A0C914C851' code='263'>
      UDP_Msg_start_point = UDP_Data.indexOf("code=") + 6;
      wx_code             = UDP_Data.substring(UDP_Msg_start_point,UDP_Data.indexOf("'>",UDP_Msg_start_point)).toInt();
      // EEPROM.write does not write to flash immediately, you must call EEPROM.commit() to save changes to flash.
      // EEPROM.end() will also commit but will release the RAM copy of EEPROM contents
      if (EEPROM.read(0) != temperature) { // Only write if the value in EEPORM is different, extends EEPROM life with limited write cycles
        EEPROM.put(0,temperature); 
        EEPROM.commit(); // Send an EEPROM write command
      }
      if (EEPROM.read(4) != wx_code) { // Only write if different from last time and remember 4-bytes for an Int variable, so addresses are 0, 4, 8 and so on
        EEPROM.put(4,wx_code);
        EEPROM.commit(); // Send an EEPROM write command
      }
    }
     //Display all the data on the TFT
    tft.fillScreen(BLACK); // Clear the screen
    tft.setTextSize(1);
    tft.setCursor(0,230);
    tft.println("V1.0");
    tft.setTextSize(4);
    tft.setTextColor(BLUE);
    if (Watts == 0) {Watts = Last_Watts;} // Removes errorenous data reception
    tft.setCursor(0,0);
    tft.print(Watts,0);
    tft.setTextSize(2);
    tft.print("Watts  ");

    //Display KW-Hour data
    tft.setTextColor(YELLOW);
    tft.setCursor(0,35);
    tft.print(KW_Hours,2);
    tft.print("KWh Used");
    
    //Display temperature data
    tft.setTextColor(GREEN);
    tft.drawLine(0,55,155,55,YELLOW);
    tft.setCursor(0,63);
    tft.print(temperature);
    tft.print(char(247)); // Deg-C symbol
    tft.print("C");

    //Display weather condition data
    tft.setTextColor(WHITE);
    DisplayWxIcon(wx_code,xpos,ypos);
    tft.setCursor(0,84);
    if (weather_conditions.length() <= 26) tft.setTextSize(2);
    //Display weather conditions icon
    if (weather_conditions.length() > 26) {
      tft.setTextSize(1);
      if (weather_conditions != weather_conditions_rx) weather_conditions = weather_conditions_rx;
      tft.print(weather_conditions);
    } else {
       if (weather_conditions != weather_conditions_rx && weather_conditions_rx != "") weather_conditions = weather_conditions_rx;
       tft.print(weather_conditions);
    }

    //Display max, min watts together with received message count 
    tft.setTextSize(1);
    tft.setTextColor(YELLOW);
    tft.setCursor(0,130);
    tft.print("Power:");
    tft.setTextColor(RED);
    tft.setCursor(0,140);
    tft.print("Max:");
    tft.print(Max_watts,0);
    tft.print("W");
    tft.setTextColor(GREEN);
    tft.setCursor(0,150);
    tft.print("Min:");
    tft.print(Min_watts,0);
    tft.print("W");
    tft.setTextColor(YELLOW);
    tft.setCursor(0,180);
    tft.print("Packets:");
    tft.setCursor(0,190);
    tft.print("Rxd:");
    tft.print(rx_count++);
    // Drawgraph(x-position, y-position, width, height, graph maximum, array, array, autoscale_on/off
    DrawGraph(95,115,200,graph_height,graph_scale,power_reading,power_cumulative, true);
    // Get current time (stamp) and display it
    time_t now = time(nullptr);
    tft.setCursor(80,232);
    tft.print("Last update: ");
    tft.print(ctime(&now));

    reading = reading + 1;
    if (reading > max_readings) { // if number of readings exceeds max_readings (e.g. 100) then shift all array data to the left to effectively scroll the display left
      reading = max_readings;
      for (int i = 1; i < max_readings; i++) {
        power_reading[i]    = power_reading[i+1];
        power_cumulative[i] = power_cumulative[i+1];
      }
      power_reading[reading]    = Watts;
      power_cumulative[reading] = KW_Hours;
    }
  }
}  


/*
Examples of weather reports:  
Code  Description
      xxxxxxxxxxxxxxxxxxxxxxxxxx
113   Clear/Sunny
116   Partly Cloudy
119   Cloudy
122   Overcast
143   Mist
176   Patchy rain nearby
179   Patchy snow nearby
182   Patchy sleet nearby
185   Patchy freezing drizzle nearby
200   Thundery outbreaks
227   Blowing snow
230   Blizzard
248   Fog
260   Freezing fog
263   Patchy light drizzle
266   Light drizzle
281   Freezing drizzle
284   Heavy freezing drizzle
293   Patchy light rain
296   Light rain
299   Moderate rain at times
302   Moderate rain
305   Heavy rain at times
308   Heavy rain
311   Light freezing rain
314   Moderate or Heavy freezing rain
317   Light sleet
320   Moderate or heavy sleet
323   Patchy light snow
326   Light snow
329   Patchy moderate snow
332   Moderate snow
335   Patchy heavy snow
338   Heavy snow
350   Ice pellets
353   Light rain shower
356   Moderate or heavy rain shower
359   Torrential rain shower
362   Light sleet showers
365   Moderate or heavy sleet showers
368   Light snow showers
371   Moderate or heavy snow showers
374   Light showers of ice pellets
377   Moderate or heavy showers of ice pellets
386   Patchy light rain in area with thunder
389   Moderate or heavy rain in area with thunder
392   Patchy light snow in area with thunder
395   Moderate or heavy snow in area with thunder
*/

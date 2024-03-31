#if 1

#include <Arduino.h>
#include "Keypad.h"
#include <Adafruit_GFX.h>
#include <MCUFRIEND_kbv.h>
#include "Eeprom24Cxx.h"
#include <EEPROM.h>
MCUFRIEND_kbv tft;
#include <TouchScreen.h>
#include "SdFat.h"
#include <SPI.h>
#define USE_SDFAT
SdFatSoftSpi<12, 11, 13> SD;  //Bit-Bang on the Shield pins

#define SD_CS 10
#define NAMEMATCH ""  // "" matches any name
//#define NAMEMATCH "tiger"    // *tiger*.bmp
#define PALETTEDEPTH 8  // support 256-colour Palette

char namebuf[32] = "/";  //BMP files in root directory
//char namebuf[32] = "/bitmaps/";  //BMP directory e.g. files in /bitmaps/*.bmp

File root;
int pathlen;

#define MINPRESSURE 20
#define MAXPRESSURE 1000

#define BLACK 0x0000
#define BLUE 0x001F
#define RED 0xF800
#define GREEN 0x07E0
#define CYAN 0x07FF
#define MAGENTA 0xF81F
#define YELLOW 0xFFE0
#define WHITE 0xFFFF
#define LIGHT_WHITE 0x5A6F72

// --- INIT LCD TFT --- //
const int XP = 7, XM = A1, YP = A2, YM = 6;  //240x320 ID=0x9325
const int TS_LEFT = 888, TS_RT = 185, TS_TOP = 939, TS_BOT = 219;
TouchScreen ts = TouchScreen(XP, YP, XM, YM, 300);
Adafruit_GFX_Button targetBtn, timeBtn;
int pixel_x, pixel_y;  //Touch_getXY() updates global vars

// --- BMP --- //
#define BMPIMAGEOFFSET 54
#define BUFFPIXEL 20

// --- KEYPAD --- //
// Constants for row and column sizes
const byte ROWS = 4;
const byte COLS = 4;

// Array to represent keys on keypad
char hexaKeys[ROWS][COLS] = {
  { '1', '2', '3', 'A' },
  { '4', '5', '6', 'B' },
  { '7', '8', '9', 'C' },
  { '*', '0', '#', 'D' }
};

// Connections to Arduino
byte rowPins[ROWS] = { 36, 34, 32, 30 };
byte colPins[COLS] = { 28, 26, 24, 22 };

// Create keypad object
Keypad customKeypad = Keypad(makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS);


// --- PASSWORD --- //
bool crePass = false;
bool veriPass = false;
String pass = "";
String displayPass = "";
String verifyPass = "";
int length = 0;
bool display = false;
char customKey;


uint16_t read16(File& f) {
  uint16_t result;  // read little-endian
  f.read(&result, sizeof(result));
  return result;
}

uint32_t read32(File& f) {
  uint32_t result;
  f.read(&result, sizeof(result));
  return result;
}

uint8_t showBMP(char* nm, int x, int y) {
  File bmpFile;
  int bmpWidth, bmpHeight;          // W+H in pixels
  uint8_t bmpDepth;                 // Bit depth (currently must be 24, 16, 8, 4, 1)
  uint32_t bmpImageoffset;          // Start of image data in file
  uint32_t rowSize;                 // Not always = bmpWidth; may have padding
  uint8_t sdbuffer[3 * BUFFPIXEL];  // pixel in buffer (R+G+B per pixel)
  uint16_t lcdbuffer[(1 << PALETTEDEPTH) + BUFFPIXEL], *palette = NULL;
  uint8_t bitmask, bitshift;
  boolean flip = true;  // BMP is stored bottom-to-top
  int w, h, row, col, lcdbufsiz = (1 << PALETTEDEPTH) + BUFFPIXEL, buffidx;
  uint32_t pos;           // seek position
  boolean is565 = false;  //

  uint16_t bmpID;
  uint16_t n;  // blocks read
  uint8_t ret;

  if ((x >= tft.width()) || (y >= tft.height()))
    return 1;  // off screen

  bmpFile = SD.open(nm);             // Parse BMP header
  bmpID = read16(bmpFile);           // BMP signature
  (void)read32(bmpFile);             // Read & ignore file size
  (void)read32(bmpFile);             // Read & ignore creator bytes
  bmpImageoffset = read32(bmpFile);  // Start of image data
  (void)read32(bmpFile);             // Read & ignore DIB header size
  bmpWidth = read32(bmpFile);
  bmpHeight = read32(bmpFile);
  n = read16(bmpFile);                                         // # planes -- must be '1'
  bmpDepth = read16(bmpFile);                                  // bits per pixel
  pos = read32(bmpFile);                                       // format
  if (bmpID != 0x4D42) ret = 2;                                // bad ID
  else if (n != 1) ret = 3;                                    // too many planes
  else if (pos != 0 && pos != 3) ret = 4;                      // format: 0 = uncompressed, 3 = 565
  else if (bmpDepth < 16 && bmpDepth > PALETTEDEPTH) ret = 5;  // palette
  else {
    bool first = true;
    is565 = (pos == 3);  // ?already in 16-bit format
    // BMP rows are padded (if needed) to 4-byte boundary
    rowSize = (bmpWidth * bmpDepth / 8 + 3) & ~3;
    if (bmpHeight < 0) {  // If negative, image is in top-down order.
      bmpHeight = -bmpHeight;
      flip = false;
    }

    w = bmpWidth;
    h = bmpHeight;
    if ((x + w) >= tft.width())  // Crop area to be loaded
      w = tft.width() - x;
    if ((y + h) >= tft.height())  //
      h = tft.height() - y;

    if (bmpDepth <= PALETTEDEPTH) {  // these modes have separate palette
      //bmpFile.seek(BMPIMAGEOFFSET); //palette is always @ 54
      bmpFile.seek(bmpImageoffset - (4 << bmpDepth));  //54 for regular, diff for colorsimportant
      bitmask = 0xFF;
      if (bmpDepth < 8)
        bitmask >>= bmpDepth;
      bitshift = 8 - bmpDepth;
      n = 1 << bmpDepth;
      lcdbufsiz -= n;
      palette = lcdbuffer + lcdbufsiz;
      for (col = 0; col < n; col++) {
        pos = read32(bmpFile);  //map palette to 5-6-5
        palette[col] = ((pos & 0x0000F8) >> 3) | ((pos & 0x00FC00) >> 5) | ((pos & 0xF80000) >> 8);
      }
    }

    // Set TFT address window to clipped image bounds
    tft.setAddrWindow(x, y, x + w - 1, y + h - 1);
    for (row = 0; row < h; row++) {  // For each scanline...
      // Seek to start of scan line.  It might seem labor-
      // intensive to be doing this on every line, but this
      // method covers a lot of gritty details like cropping
      // and scanline padding.  Also, the seek only takes
      // place if the file position actually needs to change
      // (avoids a lot of cluster math in SD library).
      uint8_t r, g, b, *sdptr;
      int lcdidx, lcdleft;
      if (flip)  // Bitmap is stored bottom-to-top order (normal BMP)
        pos = bmpImageoffset + (bmpHeight - 1 - row) * rowSize;
      else  // Bitmap is stored top-to-bottom
        pos = bmpImageoffset + row * rowSize;
      if (bmpFile.position() != pos) {  // Need seek?
        bmpFile.seek(pos);
        buffidx = sizeof(sdbuffer);  // Force buffer reload
      }

      for (col = 0; col < w;) {  //pixels in row
        lcdleft = w - col;
        if (lcdleft > lcdbufsiz) lcdleft = lcdbufsiz;
        for (lcdidx = 0; lcdidx < lcdleft; lcdidx++) {  // buffer at a time
          uint16_t color;
          // Time to read more pixel data?
          if (buffidx >= sizeof(sdbuffer)) {  // Indeed
            bmpFile.read(sdbuffer, sizeof(sdbuffer));
            buffidx = 0;  // Set index to beginning
            r = 0;
          }
          switch (bmpDepth) {  // Convert pixel from BMP to TFT format
            case 24:
              b = sdbuffer[buffidx++];
              g = sdbuffer[buffidx++];
              r = sdbuffer[buffidx++];
              color = tft.color565(r, g, b);
              break;
            case 16:
              b = sdbuffer[buffidx++];
              r = sdbuffer[buffidx++];
              if (is565)
                color = (r << 8) | (b);
              else
                color = (r << 9) | ((b & 0xE0) << 1) | (b & 0x1F);
              break;
            case 1:
            case 4:
            case 8:
              if (r == 0)
                b = sdbuffer[buffidx++], r = 8;
              color = palette[(b >> bitshift) & bitmask];
              r -= bmpDepth;
              b <<= bmpDepth;
              break;
          }
          lcdbuffer[lcdidx] = color;
        }
        tft.pushColors(lcdbuffer, lcdidx, first);
        first = false;
        col += lcdidx;
      }                                                          // end cols
    }                                                            // end rows
    tft.setAddrWindow(0, 0, tft.width() - 1, tft.height() - 1);  //restore full screen
    ret = 0;                                                     // good render
  }
  bmpFile.close();
  return (ret);
}

void writeStringToEEPROM(int addrOffset, const String& strToWrite) {
  byte len = strToWrite.length();
  EEPROM.write(addrOffset, len);

  for (int i = 0; i < len; i++) {
    EEPROM.write(addrOffset + 1 + i, strToWrite[i]);
  }
}

String readStringFromEEPROM(int addrOffset) {
  int newStrLen = EEPROM.read(addrOffset);
  char data[newStrLen + 1];

  for (int i = 0; i < newStrLen; i++) {
    data[i] = EEPROM.read(addrOffset + 1 + i);
  }
  data[newStrLen] = '\0';
  return String(data);
}

void drawTargetMode() {
  tft.fillRoundRect(50, 70, 215, 60, 5, tft.color565(37, 59, 88));
  tft.setCursor(72, 94);
  tft.setTextSize(2.5);
  tft.setTextColor(WHITE);
  tft.println("TARGET MODE");
  showBMP("Target_Flat_Icon.bmp", 210, 75);
}

void drawTimerMode() {
  tft.fillRoundRect(50, 160, 215, 60, 5, tft.color565(37, 59, 88));
  tft.setCursor(78, 183);
  tft.setTextSize(2.5);
  tft.setTextColor(WHITE);
  tft.println("TIMER MODE");
  showBMP("Calendar.bmp", 210, 167);
}

void drawPassWord(String title, int x, int y, int w, int h, int r, uint16_t color, uint8_t size, int16_t curX, int16_t curY) {
  tft.drawRoundRect(x, y, w, h, r, color);
  tft.setCursor(curX, curY);
  tft.setTextSize(size);
  tft.setTextColor(color);
  tft.println(title);
}

void keyPadControl(char customKey) {
  if (length < 6 && customKey) {
    if (customKey != 'A' && customKey != 'B' && customKey != 'C' && customKey != 'D' && customKey != '#' && customKey != '*') {
      if (crePass == false) {
        pass += customKey;
        tft.fillRoundRect(58, 88, 230, 30, 15, WHITE);
      } else if (veriPass == false) {
        verifyPass += customKey;
        tft.fillRoundRect(58, 160, 230, 30, 15, WHITE);
      }
      displayPass += "*";
      tft.setTextSize(3);
      tft.setTextColor(tft.color565(37, 59, 88));
      tft.println(displayPass);
      length++;
      Serial.println(length);
      Serial.println(customKey);
    }
  }
  if (customKey == 'C') {
    if (crePass == false) {
      pass = "";
      tft.fillRoundRect(58, 88, 230, 30, 15, WHITE);
      drawPassWord("Enter password", 20, 70, 280, 60, 15, tft.color565(69, 97, 133), 2.5, 72, 94);
    } else {
      verifyPass = "";
      tft.fillRoundRect(58, 160, 230, 30, 15, WHITE);
      drawPassWord("Re-enter password", 20, 148, 280, 60, 15, tft.color565(69, 97, 133), 2.5, 72, 170);
    }
    displayPass = "";
    length = 0;
  } else if (customKey == 'A') {
    if (crePass == false && pass != "") {
      tft.fillRoundRect(58, 88, 230, 30, 15, WHITE);
      drawPassWord(pass, 20, 70, 280, 60, 15, tft.color565(69, 97, 133), 3, 111, 90);
    } else if (veriPass == false && verifyPass != "") {
      tft.fillRoundRect(58, 160, 230, 30, 15, WHITE);
      drawPassWord(verifyPass, 20, 148, 280, 60, 15, tft.color565(69, 97, 133), 3, 111, 165);
    }
  } else if (customKey == 'B') {
    if (crePass == false && pass != "") {
      tft.fillRoundRect(58, 88, 230, 30, 15, WHITE);
      drawPassWord(displayPass, 20, 70, 280, 60, 15, tft.color565(69, 97, 133), 3, 111, 90);
    } else if (veriPass == false && verifyPass != "") {
      tft.fillRoundRect(58, 165, 230, 30, 15, WHITE);
      drawPassWord(displayPass, 20, 148, 280, 60, 15, tft.color565(69, 97, 133), 3, 111, 165);
    }
  } else if (customKey == 'D' && length >= 1) {
    if (crePass == false) {
      pass.remove(pass.length() - 1);
      tft.fillRoundRect(58, 88, 230, 30, 15, WHITE);
    } else if (veriPass == false) {
      verifyPass.remove(verifyPass.length() - 1);
      tft.fillRoundRect(58, 160, 230, 30, 15, WHITE);
    }
    displayPass.remove(displayPass.length() - 1);
    tft.setTextSize(3);
    tft.setTextColor(tft.color565(69, 97, 133));
    tft.println(displayPass);
    length--;
    if (length == 0) {
      if (crePass == false) {
        tft.fillRoundRect(58, 88, 230, 30, 15, WHITE);
        drawPassWord("Enter password", 20, 70, 280, 60, 15, tft.color565(69, 97, 133), 2.5, 72, 94);
      } else {
        tft.fillRoundRect(58, 160, 230, 30, 15, WHITE);
        drawPassWord("Re-enter password", 20, 148, 280, 60, 15, tft.color565(69, 97, 133), 2.5, 72, 170);
      }
    }
    Serial.println(length);
    Serial.println(customKey);
  }
}

void loginControl(char customKey) {
  if (length < 6 && customKey) {
    if (customKey != 'A' && customKey != 'B' && customKey != 'C' && customKey != 'D' && customKey != '#' && customKey != '*') {
      pass += customKey;
      tft.fillRoundRect(58, 128, 230, 30, 15, WHITE);
      displayPass += "*";
      tft.setTextSize(3);
      tft.setTextColor(tft.color565(37, 59, 88));
      tft.println(displayPass);
      length++;
    }
  }
  if (customKey == 'C') {
    pass = "";
    tft.fillRoundRect(58, 128, 230, 30, 15, WHITE);
    drawPassWord("Enter password", 20, 112, 280, 60, 15, tft.color565(69, 97, 133), 2.5, 85, 135);
    displayPass = "";
    length = 0;
  } else if (customKey == 'A') {
    if (pass != "") {
      tft.fillRoundRect(58, 128, 230, 30, 15, WHITE);
      drawPassWord(pass, 20, 112, 280, 60, 15, tft.color565(69, 97, 133), 3, 111, 133);
    }
  } else if (customKey == 'B') {
    if (pass != "") {
      tft.fillRoundRect(58, 128, 230, 30, 15, WHITE);
      drawPassWord(displayPass, 20, 112, 280, 60, 15, tft.color565(69, 97, 133), 3, 111, 133);
    }
  } else if (customKey == 'D' && length >= 1) {
    pass.remove(pass.length() - 1);
    tft.fillRoundRect(58, 128, 230, 30, 15, WHITE);
    displayPass.remove(displayPass.length() - 1);
    tft.setTextSize(3);
    tft.setTextColor(tft.color565(69, 97, 133));
    tft.println(displayPass);
    length--;
    if (length == 0) {
      tft.fillRoundRect(58, 128, 230, 30, 15, WHITE);
      drawPassWord("Enter password", 20, 112, 280, 60, 15, tft.color565(69, 97, 133), 2.5, 85, 135);
    }
  }
}

bool flag = false;
bool flag1 = false;

void passWord() {

  while (crePass == false) {
    if (display == false) {
      showBMP("key_password_icon.bmp", 25, 85);
      showBMP("key_password_icon.bmp", 25, 158);
      showBMP("lock_password_icon.bmp", 220, 18);
      tft.setCursor(62, 27);
      tft.setTextSize(2.95);
      tft.setTextColor(tft.color565(37, 59, 88));
      tft.println("NEW PASSWORD");
      drawPassWord("Enter password", 20, 70, 280, 60, 15, tft.color565(69, 97, 133), 2.5, 72, 94);
      drawPassWord("Re-enter password", 20, 148, 280, 60, 15, tft.color565(69, 97, 133), 2.5, 72, 170);
      display = true;
    }
    tft.setCursor(111, 90);
    customKey = customKeypad.getKey();
    keyPadControl(customKey);
    if (customKey == '#' && length == 6) {
      length = 0;
      crePass = true;
      displayPass = "";
      while (veriPass == false) {
        tft.setCursor(111, 163);
        customKey = customKeypad.getKey();
        if (customKey == '#' && length == 6 && pass == verifyPass) {
          veriPass = true;
          tft.fillScreen(WHITE);
          Serial.println(pass);
          writeStringToEEPROM(1, pass);
        } else if (customKey == '#' && pass != verifyPass) {
          tft.fillRoundRect(50, 15, 230, 50, 15, WHITE);
          tft.setCursor(50, 27);
          tft.setTextSize(2.95);
          tft.setTextColor(RED);
          tft.println("PASSWORD NOT MATCH!");
        }
        keyPadControl(customKey);
      }
    }
  }
  display = false;
}

void login() {
  crePass = false;
  veriPass = false;
  pass = "";
  displayPass = "";
  verifyPass = "";
  length = 0;
  display = false;
  while (readStringFromEEPROM(1) != "" && veriPass == false) {
    if (display == false) {
      showBMP("key_password_icon.bmp", 25, 125);
      showBMP("MAIN_lock_password_icon.bmp", 118, 18);
      tft.setCursor(62, 75);
      tft.setTextSize(3);
      tft.setTextColor(tft.color565(37, 59, 88));
      tft.println("PIGGY LOGIN");
      drawPassWord("Enter password", 20, 112, 280, 60, 15, tft.color565(69, 97, 133), 2.5, 85, 135);
      display = true;
    }
    tft.setCursor(111, 131);
    customKey = customKeypad.getKey();
    loginControl(customKey);
    if (customKey == '#' && length == 6 && pass == readStringFromEEPROM(1)) {
      veriPass = true;
      Serial.println(pass);
      writeStringToEEPROM(1, pass);
      showBMP("MAIN_lock_password_icon2.bmp", 118, 18);
      delay(1000);
      tft.fillScreen(WHITE);
    } else if (customKey == '#' && pass != readStringFromEEPROM(1)) {
      tft.fillRoundRect(50, 180, 230, 50, 15, WHITE);
      tft.setCursor(50, 190);
      tft.setTextSize(2);
      tft.setTextColor(RED);
      tft.println("PASSWORD NOT MATCH!");
    }
  }
  display = false;
}

void menuMode(void) {
  pinMode(YP, INPUT);   //restore shared pins
  pinMode(XM, OUTPUT);  //because TFT control pins
  TSPoint p = ts.getPoint();
  pinMode(YP, OUTPUT);  //restore shared pins
  pinMode(XM, OUTPUT);  //because TFT control pins
  bool pressed = (p.z > MINPRESSURE && p.z < MAXPRESSURE);
  if (pressed) {
    pixel_x = map(p.x, TS_LEFT, TS_RT, 0, 240);
    pixel_y = map(p.y, TS_TOP, TS_BOT, 0, 320);
    Serial.print("X: ");
    Serial.print(pixel_x);
    Serial.print("\t");
    Serial.print("Y: ");
    Serial.print(pixel_y);
    Serial.print("\t");
    Serial.print("Z: ");
    Serial.print(p.z);
    Serial.println("\t");
    if (pixel_x > 100 && pixel_x < 180) {
      if (pixel_y > 50 && pixel_y < 235) {
        tft.fillRoundRect(50, 70, 215, 60, 5, tft.color565(202, 248, 255));
        tft.setCursor(100, 92);
        tft.setTextSize(2.5);
        tft.setTextColor(WHITE);
        tft.println("TARGET MODE");
        showBMP("Target_Flat_Icon.bmp", 210, 75);
        flag = true;
      }
    } else if (pixel_x > 0 && pixel_x < 55) {
      if (pixel_y > 50 && pixel_y < 235) {
        tft.fillRoundRect(50, 160, 215, 60, 5, tft.color565(202, 248, 255));
        tft.setCursor(78, 183);
        tft.setTextSize(2.5);
        tft.setTextColor(WHITE);
        tft.println("TIMER MODE");
        showBMP("Calendar.bmp", 210, 167);
        flag1 = true;
      }
    }
    while (true && (flag == true || flag1 == true)) {
      p = ts.getPoint();
      if (!(p.z > MINPRESSURE && p.z < MAXPRESSURE) && (flag == true || flag1 == true)) {
        pinMode(YP, OUTPUT);  //restore shared pins
        pinMode(XM, OUTPUT);  //because TFT control pins
        if (flag == true) {
          drawTargetMode();
        } else if (flag1 == true) {
          drawTimerMode();
        }
        flag = false;
        flag1 = false;
        break;
      }
    }
  }
}


void setup(void) {
#if defined(__arm__) || defined(ESP32)  //default to 12-bit ADC
  analogReadResolution(10);             //Adafruit TouchScreen.h expects 10-bit
#endif
  Serial.begin(9600);
  uint16_t ID = tft.readID();
  Serial.print("TFT ID = 0x");
  Serial.println(ID, HEX);
  Serial.println("Calibrate for your Touch Panel");
  if (ID == 0xD3D3) ID = 0x9486;  // write-only shield
  tft.begin(ID);
  tft.setRotation(1);  //PORTRAIT
  tft.fillScreen(WHITE);
  bool good = SD.begin(SD_CS);
  if (!good) {
    Serial.print(F("cannot start SD"));
    while (1)
      ;
  }
  root = SD.open(namebuf);
  pathlen = strlen(namebuf);
  // writeStringToEEPROM(1, "");
  if (readStringFromEEPROM(1) != "") {
    login();
  } else {
    Serial.println(readStringFromEEPROM(1));
    passWord();
    login();
  }
  drawTargetMode();
  drawTimerMode();
  tft.setCursor(65, 27);
  tft.setTextSize(2.95);
  tft.setTextColor(tft.color565(37, 59, 88));
  tft.println("CHOOSE YOUR MODE");
}

/* two buttons are quite simple
 */
void loop(void) {
  // passWord();
  menuMode();
}
#endif

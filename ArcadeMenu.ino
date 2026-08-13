#include <TinyScreen.h>
#include "SdFat.h"
#include <Wire.h>
#include <SPI.h>
#include "MenuUtilAndDefs.h"
#include "TinyArcade.h"
#include "MetricBuckets.h"
#include "printStringToBuffer.h"

#define SWAP16(x) (((x) >> 8) | ((x) << 8))

TinyScreen display = TinyScreen(TinyScreenPlus);

SdFat sd;
SdFile dir;
SdFile file;
SdFile vidFile;

uint16_t buffer[96 * 64] __attribute__((aligned(4))); 
static unsigned long videoFrameTimer = 0;
const unsigned long MS_PER_FRAME = 50; // ~20 FPS (1000ms / 20)

int currentFileNum = 0;
int doGame = 0;
int doVideo = 0;
char currentGameName[50] = "  "; 

int printNameOffset = 0;
int printNameDir = 0;
unsigned long lastPrintNameOffsetChange = 0;
unsigned long printNameOffsetChangeInterval = 200;
unsigned long previousMillis = 0;
int frameCount = 0;
int currentFPS = 0;
#define AUDIO_BUF_SIZE 4096
volatile uint8_t audioRingBuffer[AUDIO_BUF_SIZE];
volatile int audioHead = 0; 
volatile int audioTail = 0;

void bufferVideoFrame(bool skipRender = false);

void setup(void) {
  arcadeInit();
  initRamProfiler();
  display.begin();
  analogWriteResolution(10); // Configure DAC to max resolution
  pinMode(A0, OUTPUT);       // Enable physical DAC Speaker pin

  display.setFont(liberationSans_8ptFontInfo);
  printCentered("Finding SD card..", 46);
  display.setBitDepth(1);
  delay(20);
  
  uint8_t* rawLogoPtr = (uint8_t*)TinyCircuits96;
  for (int x = -37; x < 5; x++) {
    display.goTo(0, constrain(x, 0, 5));
    display.startData();
    int offset = (96 * constrain(-x, 0, 37) * 2);
    display.writeBuffer(rawLogoPtr + offset, (96 * 37 * 2) - offset);
    display.endTransfer();
    delay(20 + x / 3);
  }
  display.setBitDepth(0);
  delay(250);
  
  if (!sd.begin(10, SPI_FULL_SPEED)) {
    display.clearScreen();
    printCentered("SD card not found,", 20);
    printCentered("looking for game..", 30);
    delay(500);
    if (*(uint32_t *)(APP_START_ADDRESS + 4) == 0xFFFFFFFF) {
      printCentered("No game loaded.", 40);
      printCentered("Upload or install SD", 40);
      USBDevice.init();
      USBDevice.attach();
      while (1);
    }
    SPI.end();
    SPI1.end();
    jumpApplication();
    while (1);
  }

  // FIXED: Resolve files and target directory states FIRST 
  printNextFile(); 
  
  // Only prime video buffers if a video track was actually flagged open by printNextFile
  if (doVideo) {
    bufferVideoFrame();
  }
  videoFrameTimer = millis();
  
  // FIXED: Turn on the background speaker interrupts ONLY when pointers are completely safe
  setupAudioInterrupt();
}

int blockInput = 1;
bool doDisplay = true;

void loop() {
  unsigned long currentMillis = millis();
  bool skipRender = (currentMillis - videoFrameTimer) > (MS_PER_FRAME * 2);

  profileRam();
  if (!checkJoystick(TAJoystickLeft) && !checkJoystick(TAJoystickRight)) {
    blockInput = 0;
  }
  if (!blockInput && checkJoystick(TAJoystickRight)) {
    printNextFile();
    blockInput = 1;
    videoFrameTimer = millis(); // Reset timer on file change
  }
  if (!blockInput && checkJoystick(TAJoystickLeft)) {
    printPreviousFile();
    blockInput = 1;
    videoFrameTimer = millis(); // Reset timer on file change
  }
  if (checkButton(TAButton1) && currentFileNum && false) {
    for (int y = 0; y < 64; y++) {
      for (int x = 0; x < 96; x++) {
        buffer[(y * 96) + x] = (buffer[(y * 96) + x] >> 1) & 0xEF7B; 
      }
    }
    writeToDisplay();
    writeFlash();
    SPI.end();
    SPI1.end();
    jumpApplication();
  }

  // Calculate skipRender out in the open before the timing interval check
  skipRender = (currentMillis - videoFrameTimer) > (MS_PER_FRAME * 2);

  // OPTIMIZATION: Check if it is time to process a frame
  if (currentMillis - videoFrameTimer >= MS_PER_FRAME) {
    if (doVideo) {
      // Pass the skip flag directly into your optimized video decoder
      bufferVideoFrame(skipRender);
    }
    
    // Always advance the time step uniformly to keep audio feeding smooth
    videoFrameTimer += MS_PER_FRAME; 

    if (!skipRender) {
      doDisplay = true;
      frameCount++;
    }
  }

  if (currentMillis - previousMillis >= 1000) {
    currentFPS = frameCount;
    frameCount = 0;
    previousMillis = currentMillis;
  }

  if (doGame) {
    memset(buffer, 0, 96 * 64 * 2);
    if (currentGameName[0] != '\0' && currentGameName[0] != ' ') {
      printName(currentGameName);
    }
  }

  if (checkButton(TAButton2)) {
    char statsBuffer[24]; 

    sprintf(statsBuffer, "%d", ramHistory[currentBucketIdx].minFree);
    print(statsBuffer, 1, 1, TS_16b_Green); 
    sprintf(statsBuffer, "%d", ramHistory[currentBucketIdx].maxFree);
    print(statsBuffer, 1, 9, TS_16b_Red); 
    
    for (int x = 0; x < 96; x++) {
      for (int y = 57; y < 64; y++) {
        buffer[(y * 96) + x] = TS_16b_Black;
      }
    }

    sprintf(statsBuffer, "fps: %d", currentFPS);
    printMicro(statsBuffer, 1, 58, TS_16b_Red);
  }
  if (doDisplay) {
    writeToDisplay();
    doDisplay = false;
  }
}

void printCentered(const char * printString, int y) {
  display.setCursor(48 - ((int)display.getPrintWidth((char*)printString) / 2), y);
  display.print((char*)printString);
}

void setupAudioInterrupt() {
  analogWriteResolution(10);
  pinMode(A0, OUTPUT);
  analogWrite(A0, 512); 

  REG_GCLK_CLKCTRL = (uint16_t) (GCLK_CLKCTRL_CLKEN | GCLK_CLKCTRL_GEN_GCLK0 | GCLK_CLKCTRL_ID_TC4_TC5);
  while (GCLK->STATUS.bit.SYNCBUSY);

  TC5->COUNT16.CTRLA.reg &= ~TC_CTRLA_ENABLE; 
  while (TC5->COUNT16.STATUS.bit.SYNCBUSY);

  TC5->COUNT16.CTRLA.reg |= TC_CTRLA_MODE_COUNT16 | TC_CTRLA_WAVEGEN_MFRQ | TC_CTRLA_PRESCALER_DIV64;
  while (TC5->COUNT16.STATUS.bit.SYNCBUSY);

  TC5->COUNT16.CC[0].reg = 48; 
  while (TC5->COUNT16.STATUS.bit.SYNCBUSY);

  TC5->COUNT16.INTENSET.reg = TC_INTENSET_MC0;
  NVIC_EnableIRQ(TC5_IRQn);

  TC5->COUNT16.CTRLA.reg |= TC_CTRLA_ENABLE;
  while (TC5->COUNT16.STATUS.bit.SYNCBUSY);
}

void TC5_Handler() {
  TC5->COUNT16.INTFLAG.reg = TC_INTFLAG_MC0;
  if (audioTail != audioHead) {
    uint8_t sample = audioRingBuffer[audioTail];
    audioTail = (audioTail + 1) % AUDIO_BUF_SIZE;
    analogWrite(A0, sample << 2);
  }
}

void bufferVideoFrame(bool skipRender) {
  if (!vidFile.available()) {
    vidFile.rewind(); 
  }

  uint8_t tempAudio[14]; 
  uint8_t* byteBufferPtr = (uint8_t*)buffer;

  for (int row = 0; row < 64; row++) {
    if (skipRender) {
      // OPTIMIZATION: Skip copying to RAM. Advance the SD card file pointer 
      // past 192 video bytes instantly without doing slow memory modifications.
      vidFile.seekCur(192); 
    } else {
      vidFile.read(byteBufferPtr + (row * 192), 192);
    }
    
    // CRITICAL: Always process the audio bytes regardless of video lag
    int audioCount = (row % 2 == 0) ? 13 : 12;
    vidFile.read(tempAudio, audioCount);
    
    for (int i = 0; i < audioCount; i++) {
      int nextHead = (audioHead + 1) % AUDIO_BUF_SIZE;
      if (nextHead != audioTail) {
        audioRingBuffer[audioHead] = tempAudio[i];
        audioHead = nextHead;
      }
    }
  }
}

void writeToDisplay() {
  display.setBitDepth(1);
  display.goTo(0, 0);
  display.startData();
  
  // FIXED: Removed DMA background burst to stop hardware width mismatches.
  // Standard writeBuffer safely handles streaming the raw bytes sequentially.
  display.writeBuffer((uint8_t*)buffer, 96 * 64 * 2);
  
  display.endTransfer();
  //display.setBitDepth(0);
}


bool isValidGameDir() {
  doGame = 0;
  doVideo = 0;
  SdFile file;
  
  if (dir.isDir()) {
    char path[51] = " ";
    int dirLength = 0;
    
    dir.getName(path, 50);
    dirLength = strlen(path);
    
    // Append the directory slash safely
    path[dirLength] = '/';
    
    // Read the file name directly into the path string after the slash
    dir.getName(path + dirLength + 1, 50 - (dirLength + 1));
    
    // Re-measure length to find the true end of the path string
    int fullLength = strlen(path);
    
    // Safe standard string copies without any *2 byte-multiplication
    strcpy(path + fullLength, ".bin");
    if (file.open(path, O_READ)) {
      doGame = 1;
      file.close(); // Cleanly close it so we don't leak file handles
    }
    
    strcpy(path + fullLength, ".tsv");
    if (vidFile.open(path, O_READ)) {
      doVideo = 1;
    }
  }
  return (doGame || doVideo);
}

void printNextFile() {
  int foundGame = 0;
  vidFile.close();
  while (!foundGame) {
    dir.close();
    if (dir.openNext(sd.vwd(), O_READ)) {
      currentFileNum++;
    } else {
      sd.vwd()->rewind();
      dir.openNext(sd.vwd(), O_READ);
      currentFileNum = 1;
    }
    foundGame = isValidGameDir();
  }
  dir.getName(currentGameName, 50);
  printNameOffset = 0;
  printNameDir = 0;
  lastPrintNameOffsetChange = millis();
  profileRam();
}

void printPreviousFile() {
  doVideo = 0;
  vidFile.close();
  int foundGame = 0;
  while (!foundGame) {
    if (currentFileNum > 1) {
      currentFileNum--;
    } else {
      currentFileNum = 100;
      int i;
      for (i = 1; i < currentFileNum; i++) {
        dir.close();
        if (!dir.openNext(sd.vwd(), O_READ)) break;
      }
      currentFileNum = i;
    }
    sd.vwd()->rewind();
    int i;
    for (i = 1; i <= currentFileNum; i++) {
      dir.close();
      if (!dir.openNext(sd.vwd(), O_READ)) break;
    }
    foundGame = isValidGameDir();
  }
  dir.getName(currentGameName, 50);
  printNameOffset = 0;
  printNameDir = 0;
  lastPrintNameOffsetChange = millis();
  profileRam();
}

void printName(char * name) {
  int yOffset = 34;
  for (int y = yOffset + 0; y < yOffset + 12; y++) {
    for (int x = 0; x < 96; x++) {
      buffer[(y * 96) + x] = (buffer[(y * 96) + x] >> 2) & 0xE739; 
    }
  }
  
  // FIXED: Restored explicit array allocation bounds [50] to prevent stack corruption
  char displayName[50]; 
  strcpy(displayName, name + printNameOffset);

  int xPosition = 48 - ((int)display.getPrintWidth(displayName) / 2);

  if (millis() > lastPrintNameOffsetChange + printNameOffsetChangeInterval) {
    lastPrintNameOffsetChange = millis();
    if (xPosition < 9) {
      printNameOffset++;
    } else {
      printNameOffset = 0;
    }
  }

  while (xPosition < 9) {
    displayName[strlen(displayName) - 1] = '\0';
    xPosition = 48 - ((int)display.getPrintWidth(displayName) / 2);
  }

  for (int y = yOffset + 1; y < yOffset + 15; y++) {
    putString(y, xPosition, yOffset + 1, displayName, buffer, thinPixel7_10ptFontInfo);
    putString(y, 1, yOffset + 1, "<", buffer, thinPixel7_10ptFontInfo);
    putString(y, 87 + 5, yOffset + 1, ">", buffer, thinPixel7_10ptFontInfo);
  }
}

void putString(int y, int fontX, int fontY, char * string, uint16_t * buff, const FONT_INFO & fontInfo) {
  const FONT_CHAR_INFO* fontDescriptor = fontInfo.charDesc;
  int fontHeight = fontInfo.height;
  if (y >= fontY && y < fontY + fontHeight) {
    const unsigned char* fontBitmap = fontInfo.bitmap;
    int fontFirstCh = fontInfo.startCh;
    int fontLastCh = fontInfo.endCh;
    int stringChar = 0;
    int ch = string[stringChar++];
    while (ch) {
      uint8_t chWidth = pgm_read_byte(&fontDescriptor[ch - fontFirstCh].width);
      int bytesPerRow = chWidth / 8;
      if (chWidth > bytesPerRow * 8)
        bytesPerRow++;
      unsigned int offset = pgm_read_word(&fontDescriptor[ch - fontFirstCh].offset) + (bytesPerRow * fontHeight) - 1;

      for (uint8_t byte = 0; byte < bytesPerRow; byte++) {
        uint8_t data = pgm_read_byte(fontBitmap + offset - (y - fontY) - ((bytesPerRow - byte - 1) * fontHeight));
        uint8_t bits = byte * 8;
        
        // Use a local variable to step horizontally while keeping fontX intact
        int currentX = fontX;
        for (int i = 0; i < 8 && (bits + i) < chWidth; i++) {
          if (data & (0x80 >> i)) {
            // FIXED: Prevent out-of-bounds screen edge memory corruption
            if (currentX >= 0 && currentX < 96 && y >= 0 && y < 64) {
              buff[(y * 96) + currentX] = 0xFFFF; 
            }
            if (checkButton(TAButton1))
              profileRam();
          }
          currentX++;
        }
      }
      fontX += chWidth + 1; // Correctly advance to next character start position
      ch = string[stringChar++];
    }
  }
}

void showStatusBar(int state, unsigned int color1, unsigned int color2) {
  for (int y = 20; y < 24; y++) {
    for (int x = 0; x < 96; x++) {
      if (x < state)
        buffer[(y * 96) + x] = color1;
      else
        buffer[(y * 96) + x] = color2;
    }
  }
  //display.setBitDepth(1);
  display.goTo(0, 20);
  display.startData();
  
  // FIXED: Standardize layout index offsets to 16-bit pixel bounds 
  // Row 20 * 96 pixels wide. Total byte size remains 96 wide * 4 lines * 2 bytes.
  display.writeBuffer((uint8_t*)(buffer + (96 * 20)), 96 * 4 * 2);
  
  display.endTransfer();
  display.setBitDepth(0);
}

void writeFlash() {
  uint32_t address = 64 * 1024;
  uint32_t PAGE_SIZE, PAGES, MAX_FLASH;
  uint32_t pageSizes[] = { 8, 16, 32, 64, 128, 256, 512, 1024 };
  PAGE_SIZE = pageSizes[NVMCTRL->PARAM.bit.PSZ];
  PAGES = NVMCTRL->PARAM.bit.NVMP;
  MAX_FLASH = PAGE_SIZE * PAGES;

  uint32_t erase_dst_addr = address; 
  int lastDisplayedValue = -1;

  while (erase_dst_addr < MAX_FLASH) {
    NVMCTRL->ADDR.reg = erase_dst_addr / 2;
    NVMCTRL->CTRLA.reg = NVMCTRL_CTRLA_CMDEX_KEY | NVMCTRL_CTRLA_CMD_ER;

    uint32_t newDisplayValue = ((erase_dst_addr - address) * 96) / (MAX_FLASH - address);
    if (newDisplayValue != lastDisplayedValue) {
      showStatusBar(newDisplayValue, 0x1F00, 0x0000);
      lastDisplayedValue = newDisplayValue;
    }

    while (NVMCTRL->INTFLAG.bit.READY == 0);
    erase_dst_addr += PAGE_SIZE * 4; 
  }

  lastDisplayedValue = -1;

  uint32_t size = file.fileSize(); 
  uint32_t *dst_addr = (uint32_t *)(64 * 1024);

  NVMCTRL->CTRLB.bit.MANW = 0;
  NVMCTRL->ADDR.reg = address / 2;
  
  while (size) {
    // FIXED: Explicitly cast 'buffer' to uint8_t* to force 1-byte file reading strides
    int amtBytes = file.read((uint8_t*)buffer, PAGE_SIZE);

    if (size < 64) {
      uint8_t* byteBuf = (uint8_t*)buffer;
      for (int j = size; j < PAGE_SIZE; j++) byteBuf[j] = 0xFF;
    }
    NVMCTRL->CTRLA.reg = NVMCTRL_CTRLA_CMDEX_KEY | NVMCTRL_CTRLA_CMD_PBC;
    while (NVMCTRL->INTFLAG.bit.READY == 0);

    uint32_t i;
    for (i = 0; i < (PAGE_SIZE / 4) && i < size; i++) {
      dst_addr[i] = ((uint32_t *)buffer)[i];
    }

    NVMCTRL->CTRLA.reg = NVMCTRL_CTRLA_CMDEX_KEY | NVMCTRL_CTRLA_CMD_WP;

    int newDisplayValue = 96 - (size * 96) / file.fileSize();
    if (newDisplayValue != lastDisplayedValue) {
      showStatusBar(newDisplayValue, 0xE007, 0x1F00);
      lastDisplayedValue = newDisplayValue;
    }

    while (NVMCTRL->INTFLAG.bit.READY == 0);

    dst_addr += i;
    if (size > PAGE_SIZE)
      size -= PAGE_SIZE;
    else
      size = 0;
  }
}
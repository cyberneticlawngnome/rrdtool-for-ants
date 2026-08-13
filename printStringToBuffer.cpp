#include "printStringToBuffer.h"

extern uint16_t buffer[]; // Safely tracks the native global 16-bit array mapping

// High-density 3x5 layout map: 26 lowercase entries + 10 numbers + 10 symbols = 46 entries total
const uint8_t tailoredMicroFont[46][5] PROGMEM = {
  // Lowercase alphabet entries (Indices 0 to 25)
  {0x0,0x7,0x9,0x9,0x7}, // a
  {0x8,0x8,0xE,0x9,0xE}, // b
  {0x0,0x7,0x8,0x8,0x7}, // c
  {0x1,0x1,0xF,0x9,0xF}, // d
  {0x0,0x6,0xB,0x8,0x7}, // e
  {0x3,0x4,0x7,0x4,0x4}, // f
  {0x0,0x7,0x9,0x7,0x1}, // g
  {0x8,0x8,0xE,0x9,0x9}, // h
  {0x4,0x0,0x4,0x4,0x4}, // i
  {0x1,0x0,0x1,0x1,0x6}, // j
  {0x8,0x9,0xA,0xC,0xA}, // k
  {0x4,0x4,0x4,0x4,0x3}, // l
  {0x0,0x15,0x15,0x15,0x15}, // m
  {0x0,0xC,0x9,0x9,0x9}, // n
  {0x0,0x6,0x9,0x9,0x6}, // o
  {0x0,0xE,0x9,0xE,0x8}, // p
  {0x0,0x7,0x9,0x7,0x1}, // q
  {0x0,0x5,0x6,0x4,0x4}, // r
  {0x0,0x7,0x4,0x3,0xE}, // s
  {0x4,0x7,0x4,0x4,0x3}, // t
  {0x0,0x9,0x9,0x9,0x7}, // u
  {0x0,0x9,0x9,0xA,0x4}, // v
  {0x0,0x15,0x15,0x15,0xA}, // w
  {0x0,0x9,0x4,0x9,0x9}, // x
  {0x0,0x9,0x9,0x7,0x1}, // y
  {0x0,0x7,0x2,0x4,0x7}, // z

  // Numeric entries (Indices 26 to 35)
  {0x6,0x9,0x9,0x9,0x6}, // 0
  {0x2,0x6,0x2,0x2,0x7}, // 1
  {0x6,0x1,0x6,0x8,0xF}, // 2
  {0xF,0x2,0x4,0x1,0xE}, // 3
  {0x9,0x9,0xF,0x1,0x1}, // 4
  {0xF,0x8,0xE,0x1,0xE}, // 5
  {0x6,0x8,0xE,0x9,0x6}, // 6
  {0xF,0x1,0x2,0x4,0x4}, // 7
  {0x6,0x9,0x6,0x9,0x6}, // 8
  {0x6,0x9,0x7,0x1,0x6}, // 9

  // Targeted symbols layout map entries (Indices 36 to 45)
  {0x0,0x0,0x0,0x0,0x4}, // .
  {0x0,0x0,0x4,0x0,0x4}, // ,
  {0x4,0xE,0x4,0xB,0x4}, // $
  {0x4,0x4,0x4,0x0,0x4}, // !
  {0x0,0x4,0x0,0x4,0x0}, // :
  {0x0,0x0,0x0,0x0,0x0}, // Space ' '
  {0x0,0x4,0x4,0x0,0x0}, // +
  {0x0,0x0,0x6,0x0,0x0}, // -
  {0x6,0x1,0x2,0x0,0x2}, // ?
  {0x4,0x4,0x0,0x0,0x0}  // Single Quote '
};

const uint16_t BLACK = 0x0000;
const uint16_t GREEN = 0x07E0;

int getMappedFontIndex(char c) {
  if (c >= 'A' && c <= 'Z') c += 32; 
  if (c >= 'a' && c <= 'z') return c - 'a';
  if (c >= '0' && c <= '9') return 26 + (c - '0');
  
  switch(c) {
    case '.': return 36;
    case ',': return 37;
    case '$': return 38;
    case '!': return 39;
    case ':': return 40;
    case ' ': return 41;
    case '+': return 42;
    case '-': return 43;
    case '?': return 44;
    case '\'': return 45;
    default:  return 41; 
  }
}

uint16_t c8to16(uint8_t c8B) {
  uint8_t r8 = (c8B & 0xE0);       
  uint8_t g8 = (c8B & 0x1C) << 3;  
  uint8_t b8 = (c8B & 0x03) << 6;  

  uint16_t c16B = ((r8 | (r8 >> 3) | (r8 >> 6)) >> 3) << 11 |
                  ((g8 | (g8 >> 3) | (g8 >> 6)) >> 2) << 5  |
                  ((b8 | (b8 >> 2) | (b8 >> 4) | (b8 >> 6)) >> 3);
  return c16B;
}

void printMicro(const char* str, int x, int y, uint16_t color, uint16_t bgColor) {
  while (*str) {
    int fontIdx = getMappedFontIndex(*str);

    for (int row = 0; row < 5; row++) {
      uint8_t line = pgm_read_byte(&(tailoredMicroFont[fontIdx][row]));
      
      for (int col = 0; col < 3; col++) {
        int targetX = x + col;
        int targetY = y + row;

        if (targetX >= 0 && targetX < 96 && targetY >= 0 && targetY < 64) {
          if (line & (0x04 >> col)) {
            buffer[(targetY * 96) + targetX] = color;
          } else if (color != bgColor) {
            buffer[(targetY * 96) + targetX] = bgColor;
          }
        }
      }
    }

    if (color != bgColor) {
      int spaceX = x + 3;
      if (spaceX >= 0 && spaceX < 96) {
        for (int row = 0; row < 5; row++) {
          if (y + row >= 0 && y + row < 64) {
            buffer[((y + row) * 96) + spaceX] = bgColor;
          }
        }
      }
    }

    x += 4; 
    str++;
  }
} 

void print(const char* str, int x, int y, uint16_t color, uint16_t bgColor) {
  // Read primitive font attributes safely from flash memory structures
  uint8_t fontHeight = pgm_read_byte(&(ACTIVE_FONT.height));
  char startChar = pgm_read_byte(&(ACTIVE_FONT.startCh));
  char endChar = pgm_read_byte(&(ACTIVE_FONT.endCh));

  while (*str) {
    char c = *str;
    
    if (c < startChar || c > endChar) {
      c = ' '; 
    }

    int fontIdx = c - startChar;
    
    // FIXED: Bypass struct constraints by extracting the width and offset bytes
    // directly from the PROGMEM flash address layout.
    uint8_t charWidth = pgm_read_byte(&(ACTIVE_FONT.charDesc[fontIdx].width));
    uint16_t bitmapOffset = pgm_read_word(&(ACTIVE_FONT.charDesc[fontIdx].offset));
    
    uint8_t bytesPerRow = (charWidth + 7) / 8;

    // Fetch the raw pointer address of the font's bitmap block inside flash
    const unsigned char* bitmapPtr = (const unsigned char*)pgm_read_word(&(ACTIVE_FONT.bitmap));

    for (int row = 0; row < fontHeight; row++) {
      for (int b = 0; b < bytesPerRow; b++) {
        uint16_t currentByteOffset = bitmapOffset + (row * bytesPerRow) + b;
        uint8_t line = pgm_read_byte(bitmapPtr + currentByteOffset);
        
        for (int bit = 0; bit < 8; bit++) {
          int col = (b * 8) + bit;
          if (col >= charWidth) break; 

          int targetX = x + col;
          int targetY = y + (fontHeight - 1 - row);

          if (targetX >= 0 && targetX < 96 && targetY >= 0 && targetY < 64) {
            if (line & (0x80 >> bit)) {
              buffer[(targetY * 96) + targetX] = color;
            } else {
              buffer[(targetY * 96) + targetX] = bgColor;
            }
          }
        }
      }
    }

    int spaceX = x + charWidth;
    if (spaceX >= 0 && spaceX < 96) {
      for (int row = 0; row < fontHeight; row++) {
        int targetY = y + row;
        if (targetY >= 0 && targetY < 64) {
          buffer[(targetY * 96) + spaceX] = bgColor;
        }
      }
    }

    x += charWidth + 1; 
    str++;
  }
}
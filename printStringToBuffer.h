#ifndef PSTB_H
#define PSTB_H

#include <Arduino.h>
#include <TinyScreen.h>

#define ACTIVE_FONT liberationSans_8ptFontInfo

uint16_t c8to16(uint8_t c8B);
 
void print(
    const char* str,
    int x = 0,
    int y = 0,
    uint16_t color = TS_16b_Green,
    uint16_t bgColor = TS_16b_Black);

void printMicro(
    const char* str,
    int x = 0,
    int y = 0,
    uint16_t color = TS_16b_Green,
    uint16_t bgColor = TS_16b_Black);

#endif
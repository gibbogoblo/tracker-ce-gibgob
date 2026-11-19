#pragma once
#include <stdint.h>
#define RECTS 9
#define BITMAPS 1
#define RGB565 7
#define SRC_ALPHA 2
#define ONE_MINUS_SRC_ALPHA 4
#define OPT_CENTERX 512
#define OPT_CENTERY 1024
#define OPT_CENTER 1536UL
#define OPT_RIGHTX 2048
void API_COLOR(uint32_t c);
void API_COLOR_A(uint8_t a);
void API_BEGIN(int prim);
void API_END();
void API_VERTEX2F(int16_t x, int16_t y);
void API_LINE_WIDTH(int w);
void API_CMD_SETBITMAP(uint32_t source, uint16_t fmt, uint16_t w, uint16_t h);
void API_BLEND_FUNC(int a, int b);
void API_CMD_TEXT(int16_t x, int16_t y, int16_t font, uint16_t options, const char* s);

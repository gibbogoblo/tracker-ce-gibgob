#pragma once
#include "displayControls.h"
struct strFont { int handle; };
extern strFont fonts[5];
// the display: only what the GibGob controls call
struct cDisplay
{
    uint32_t ramGScratches = 0;
    int ramGWrites = 0;
    uint32_t ramGBytes = 0;
    void writeRamG(const uint8_t*, uint32_t size, uint32_t) { ramGWrites++; ramGBytes += size; }
};
extern cDisplay display;

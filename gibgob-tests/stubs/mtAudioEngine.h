#pragma once
#include <stdint.h>
struct playerEngine
{
    int index = 0;
    uint8_t noteOn(uint8_t instr, int8_t note, int8_t vel, uint8_t, uint8_t, uint8_t, uint8_t);
    void noteOff(int8_t option = -4);
};
extern playerEngine instrumentPlayer[8];

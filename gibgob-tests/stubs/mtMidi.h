#pragma once
#include <stdint.h>
void sendMidiNoteOn(uint8_t note, uint8_t velo, uint8_t channel, bool toSynth);
void sendMidiNoteOff(uint8_t note, uint8_t velo, uint8_t channel, bool toSynth);
void sendCC(uint8_t index, uint8_t value, uint8_t channel, bool toSynth);

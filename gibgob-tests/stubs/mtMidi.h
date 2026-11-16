#pragma once
#include <stdint.h>
void sendMidiNoteOn(uint8_t note, uint8_t velo, uint8_t channel, bool toSynth);
void sendMidiNoteOff(uint8_t note, uint8_t velo, uint8_t channel, bool toSynth);

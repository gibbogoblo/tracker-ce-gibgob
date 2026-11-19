#pragma once

#include <cstddef>
#include <cstdint>

// GibGob OS: how the plugin (.psyn) memory is doing, for the synth editor.
namespace polysynth
{
struct PsynStatus
{
    size_t arenaTotal = 0;    // the plugin memory (code, voices, effect buffers)
    size_t arenaUsed = 0;
    uint32_t voiceFailures = 0; // voices that could not be made (no memory)
};
PsynStatus psynStatus();
} // namespace polysynth

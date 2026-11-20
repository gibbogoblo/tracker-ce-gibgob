#ifndef GIBGOB_GRAINRECORDER_H_
#define GIBGOB_GRAINRECORDER_H_

// GibGob OS: recording straight into a synth's sample slot (GORILLAGRAIN).
// Uses the Mini's recorder with the input and gain set in the Sample
// Recorder (line in, mic or USB), into the free end of the sample memory.
// The take is saved as <engine folder>/REC_nn.wav and loaded into the slot.

#include <stdint.h>

namespace gibgob
{
namespace grainrec
{

static constexpr int kPeakBins = 80;
static constexpr int kMaxSeconds = 20;

struct State
{
    bool active;
    uint32_t frames;    // recorded so far
    uint32_t maxFrames; // when it stops on its own
    float level;        // the input now, 0..1
    uint8_t peaks[kPeakBins];
    bool full;          // reached maxFrames (the caller saves it)
};

// starts recording; false (with why in `why`) if it cannot
bool start(char* why, int whyLen);
// call often while active: follows the recording for the picture
void poll();
const State& state();
// stops; saves the take into `folder` and returns its full path in `path`
// (empty if nothing was recorded or saving failed: why in `why`)
bool stopAndSave(const char* folder, char* path, int pathLen, char* why, int whyLen);
// stops without keeping anything
void cancel();

} // namespace grainrec
} // namespace gibgob

#endif

#ifndef GIBGOB_CHORDFEEL_H_
#define GIBGOB_CHORDFEEL_H_

// GibGob OS: how a chord played live sounds (per track): strummed, run as an
// arpeggio, or humanized like a player's hands. ChordPlayer turns one held
// chord into timed note-ons and note-offs. No hardware here (host tested).

#include <stdint.h>

namespace gibgob
{

enum StrumDir : uint8_t
{
    kStrumUp = 0,   // low note first
    kStrumDown,     // high note first
    kStrumAlt,      // up, then down, then up ... (each chord turns round)
    kStrumDirCount
};

struct ChordFeel
{
    uint8_t strum = 0;    // 0 off, 1 (slow, harp) .. 10 (tight)
    uint8_t strumDir = kStrumUp;
    uint8_t arpMode = 0;  // 0 off, else an ArpMode (Up .. As placed): the held chord runs as an
                          // arpeggio at the track's arp rate, octaves and gate
    uint8_t humTime = 0;  // 0 .. 10: notes land up to 4 ms * this late
    uint8_t humVel = 0;   // 0 .. 10: velocity up to +/- 3 * this

    bool any() const { return strum > 0 || arpMode != 0 || humTime > 0 || humVel > 0; }
};

// milliseconds between two strummed notes
int strumSpacingMs(int strum);

// the arp part of the track's settings (see TrackSettings)
struct ArpConf
{
    uint8_t mode;       // ArpMode (the feel's arpMode)
    uint8_t rateTicks;  // micro steps per arp note
    uint8_t octaves;    // 1..4
    uint8_t gate;       // 10..100 %
};

class ChordPlayer
{
  public:
    struct Out
    {
        virtual void on(uint8_t pitch, uint8_t velocity) = 0;
        virtual void off(uint8_t pitch) = 0;
        virtual ~Out() = default;
    };

    // A chord pressed at `nowMs`. msPerTick: the length of a micro step
    // (1/48 step) at the tempo. synced: the sequencer plays, playTick is
    // its position (micro steps, any running count); the arp follows its grid.
    void start(const uint8_t* pitches, int count, uint8_t velocity, const ChordFeel& feel, const ArpConf& arp,
               uint32_t nowMs, float msPerTick, bool synced, int playTick, Out& out);
    // released: everything sounding stops, nothing more starts
    void stop(Out& out);
    // call often (each UI loop); plays what is due
    void poll(uint32_t nowMs, float msPerTick, bool synced, int playTick, Out& out);
    bool active() const { return held || eventCount > 0; }

  private:
    struct Event
    {
        uint8_t pitch;
        uint8_t velocity;
        bool started;
        uint32_t onAt;
        uint32_t offAt; // 0: until released
    };
    static constexpr int kMaxEvents = 24;
    static constexpr int kMaxNotes = 8;

    void add(uint8_t pitch, uint8_t velocity, uint32_t onAt, uint32_t offAt);
    void arpStep(uint32_t nowMs, float msPerTick);
    uint32_t humanDelay();
    uint8_t humanVelocity(int velocity);
    uint32_t rand32();

    Event events[kMaxEvents];
    int eventCount = 0;
    bool held = false;
    ChordFeel f;
    ArpConf a;
    uint8_t order[kMaxNotes]; // the chord, low to high (or as pressed)
    int noteCount = 0;
    uint8_t velocity = 100;
    // arp
    int arpIdx = 0;
    uint32_t nextArpAt = 0;
    int lastGrid = -1;
    bool strumUpNext = true; // kStrumAlt: the next chord's direction
    uint32_t seed = 0x9E3779B9u;
};

} // namespace gibgob

#endif

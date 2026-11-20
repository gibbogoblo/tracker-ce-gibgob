#include "gibgob/ChordFeel.h"

namespace gibgob
{

namespace
{
int clampInt(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}
// ArpMode values (MelodicTrack.h), kept here so this file stands alone
constexpr uint8_t kModeOff = 0, kModeUp = 1, kModeDown = 2, kModeUpDown = 3, kModeRandom = 4, kModeAsPlaced = 5;
} // namespace

int strumSpacingMs(int strum)
{
    // 1: 130 ms a note (a slow harp) .. 10: 13 ms (a quick strum)
    strum = clampInt(strum, 1, 10);
    return 143 - strum * 13;
}

uint32_t ChordPlayer::rand32()
{
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}

uint32_t ChordPlayer::humanDelay()
{
    if (f.mode != kFeelHuman || f.humTime == 0)
        return 0;
    return rand32() % (uint32_t)(clampInt(f.humTime, 0, kHumanMax) * kHumanMsPerStep + 1);
}

uint8_t ChordPlayer::humanVelocity(int v)
{
    if (f.mode == kFeelHuman && f.humVel > 0)
    {
        const int depth = clampInt(f.humVel, 0, kHumanMax) * kHumanVelPerStep;
        v += (int)(rand32() % (uint32_t)(2 * depth + 1)) - depth;
    }
    return (uint8_t)clampInt(v, 1, 127);
}

void ChordPlayer::add(uint8_t pitch, uint8_t vel, uint32_t onAt, uint32_t offAt)
{
    if (eventCount >= kMaxEvents)
        return;
    events[eventCount++] = Event{ pitch, vel, false, onAt, offAt };
}

void ChordPlayer::start(const uint8_t* pitches, int count, uint8_t vel, const ChordFeel& feel, const ArpConf& arp,
                        uint32_t nowMs, float msPerTick, bool synced, int playTick, Out& out)
{
    stop(out);
    if (pitches == nullptr || count <= 0)
        return;
    f = feel;
    a = arp;
    if (a.octaves < 1)
        a.octaves = 1;
    if (a.rateTicks < 1)
        a.rateTicks = 12;
    velocity = vel;
    seed ^= nowMs * 2654435761u + 1u;
    noteCount = count > kMaxNotes ? kMaxNotes : count;
    for (int i = 0; i < noteCount; i++)
        order[i] = pitches[i] > 127 ? 127 : pitches[i];
    // low to high (the arp keeps the order pressed in its As placed mode)
    if (!(f.mode == kFeelArp && a.mode == kModeAsPlaced))
        for (int x = 0; x < noteCount; x++)
            for (int y = x + 1; y < noteCount; y++)
                if (order[y] < order[x])
                {
                    const uint8_t t = order[x];
                    order[x] = order[y];
                    order[y] = t;
                }
    held = true;

    if (f.mode == kFeelArp)
    {
        arpIdx = 0;
        lastGrid = synced ? playTick / a.rateTicks : -1;
        arpStep(nowMs, msPerTick); // the first note at once
        nextArpAt = nowMs + (uint32_t)(a.rateTicks * msPerTick + 0.5f);
        return;
    }

    // the chord: strummed (each note a little after the one before) and humanized
    bool up = true;
    if (f.mode != kFeelStrum)
        ; // the chord (or humanized): no strum
    else if (f.strumDir == kStrumDown)
        up = false;
    else if (f.strumDir == kStrumAlt)
    {
        up = strumUpNext;
        strumUpNext = !strumUpNext;
    }
    const int spacing = f.mode == kFeelStrum ? strumSpacingMs(f.strum) : 0;
    for (int i = 0; i < noteCount; i++)
    {
        const uint8_t pitch = order[up ? i : noteCount - 1 - i];
        add(pitch, humanVelocity(velocity), nowMs + (uint32_t)(i * spacing) + humanDelay(), 0);
    }
    poll(nowMs, msPerTick, synced, playTick, out);
}

void ChordPlayer::arpStep(uint32_t nowMs, float msPerTick)
{
    if (noteCount <= 0)
        return;
    const int total = noteCount * a.octaves;
    const int idx = arpIdx++;
    int k = 0;
    switch (a.mode)
    {
        case kModeDown: k = total - 1 - (idx % total); break;
        case kModeUpDown:
        {
            const int period = total > 1 ? 2 * total - 2 : 1;
            const int m = idx % period;
            k = m < total ? m : period - m;
            break;
        }
        case kModeRandom: k = (int)(rand32() % (uint32_t)total); break;
        default: k = idx % total; break; // up, as placed, off
    }
    const int pitch = clampInt(order[k % noteCount] + 12 * (k / noteCount), 0, 127);
    const float stepMs = a.rateTicks * msPerTick;
    uint32_t gateMs = (uint32_t)(stepMs * clampInt(a.gate, 10, 100) / 100.0f);
    if (gateMs < 10)
        gateMs = 10;
    const uint32_t onAt = nowMs + humanDelay();
    add((uint8_t)pitch, humanVelocity(velocity), onAt, onAt + gateMs);
}

void ChordPlayer::poll(uint32_t nowMs, float msPerTick, bool synced, int playTick, Out& out)
{
    // the arp clock: the sequencer's grid while it plays, else the tempo
    if (held && f.mode == kFeelArp)
    {
        if (synced)
        {
            const int grid = playTick / a.rateTicks;
            if (lastGrid < 0)
                lastGrid = grid;
            else if (grid != lastGrid)
            {
                lastGrid = grid;
                arpStep(nowMs, msPerTick);
            }
        }
        else if ((int32_t)(nowMs - nextArpAt) >= 0)
        {
            arpStep(nowMs, msPerTick);
            const uint32_t period = (uint32_t)(a.rateTicks * msPerTick + 0.5f);
            nextArpAt += period > 0 ? period : 1;
            if ((int32_t)(nowMs - nextArpAt) > 0)
                nextArpAt = nowMs + period; // fell behind: no burst of notes
        }
    }

    // note-offs first (an arp note may come back on the same pitch)
    for (int i = 0; i < eventCount;)
    {
        Event& e = events[i];
        if (e.started && e.offAt != 0 && (int32_t)(nowMs - e.offAt) >= 0)
        {
            out.off(e.pitch);
            events[i] = events[--eventCount];
            continue;
        }
        i++;
    }
    for (int i = 0; i < eventCount; i++)
    {
        Event& e = events[i];
        if (!e.started && (int32_t)(nowMs - e.onAt) >= 0)
        {
            out.on(e.pitch, e.velocity);
            e.started = true;
        }
    }
}

void ChordPlayer::stop(Out& out)
{
    for (int i = 0; i < eventCount; i++)
        if (events[i].started)
            out.off(events[i].pitch);
    eventCount = 0;
    held = false;
}

} // namespace gibgob

#include "gibgob/Whistle.h"

#include <math.h>
#include <string.h>

namespace gibgob
{
namespace whistle
{

namespace
{
constexpr float kVoicedClarity = 0.72f; // YIN: d' below 0.28
constexpr float kMinPitch = 66.0f;      // F#4: lower is not a whistle (hum, noise)
constexpr float kMaxPitch = 111.0f;
constexpr float kMovingSlope = 14.0f;   // semitones a second: faster = between notes
constexpr int kMinNoteHops = 7;         // 41 ms: shorter is not a note
constexpr int kMinStableHops = 5;       // 29 ms the pitch holds still
constexpr int kSlideHops = 13;          // 75 ms: a slower change between notes is a slide
constexpr float kGlideSemis = 1.3f;     // a glide goes at least this far one way
constexpr float kGlideSlope = 4.5f;     // at this many semitones a second or faster

float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

// the median of up to 64 values (a copy is sorted)
float median(const float* v, int n)
{
    float t[64];
    if (n > 64)
    {
        // long: every k-th value
        const int k = (n + 63) / 64;
        int m = 0;
        for (int i = 0; i < n && m < 64; i += k)
            t[m++] = v[i];
        n = m;
    }
    else
        memcpy(t, v, sizeof(float) * (size_t)n);
    for (int i = 1; i < n; i++)
    {
        float x = t[i];
        int j = i - 1;
        while (j >= 0 && t[j] > x)
        {
            t[j + 1] = t[j];
            j--;
        }
        t[j + 1] = x;
    }
    return n == 0 ? 0.0f : ((n & 1) ? t[n / 2] : 0.5f * (t[n / 2 - 1] + t[n / 2]));
}

// the scale steps of a major / natural minor key
const uint8_t kMajor[7] = { 0, 2, 4, 5, 7, 9, 11 };
const uint8_t kMinor[7] = { 0, 2, 3, 5, 7, 8, 10 };

bool inScale(int semis, uint8_t snap, int key)
{
    if (snap == kSnapChromatic)
        return true;
    const uint8_t* s = snap == kSnapMajor ? kMajor : kMinor;
    const int pc = ((semis - key) % 12 + 12) % 12;
    for (int i = 0; i < 7; i++)
        if (s[i] == pc)
            return true;
    return false;
}
} // namespace

// ---------------------------------------------------------------- pitch ----

Reading PitchTracker::analyse(const int16_t* pcm)
{
    Reading r{ 0.0f, 0.0f, 0.0f };
    // (straight from the 16-bit samples: no copy on the stack)
    float energy = 0.0f;
    for (int i = 0; i < kWindow; i++)
        energy += (float)pcm[i] * (float)pcm[i];
    r.level = sqrtf(energy / kWindow) * (1.0f / 32768.0f);
    if (r.level < 1e-4f)
        return r;

    // YIN: the difference function and its cumulative mean normalised form
    float d[kMaxLag + 2];
    d[0] = 0.0f;
    float sum = 0.0f;
    float dn[kMaxLag + 2];
    dn[0] = 1.0f;
    for (int tau = 1; tau <= kMaxLag + 1; tau++)
    {
        float s = 0.0f;
        const int16_t* a = pcm;
        const int16_t* b = pcm + tau;
        for (int i = 0; i < kWindow; i++)
        {
            const float v = (float)(a[i] - b[i]);
            s += v * v;
        }
        d[tau] = s;
        sum += s;
        dn[tau] = sum > 0.0f ? s * (float)tau / sum : 1.0f;
    }

    // the first clear dip (else the deepest)
    int best = -1;
    for (int tau = kMinLag; tau <= kMaxLag; tau++)
    {
        if (dn[tau] < 0.2f)
        {
            while (tau + 1 <= kMaxLag && dn[tau + 1] < dn[tau])
                tau++;
            best = tau;
            break;
        }
    }
    if (best < 0)
    {
        best = kMinLag;
        for (int tau = kMinLag; tau <= kMaxLag; tau++)
            if (dn[tau] < dn[best])
                best = tau;
    }
    r.clarity = clampf(1.0f - dn[best], 0.0f, 1.0f);

    // the period, finer: measured over as many whole periods as fit
    auto refine = [&](int t) -> float {
        if (t <= 1 || t >= kMaxLag + 1)
            return (float)t;
        const float a = d[t - 1], b = d[t], c = d[t + 1];
        const float den = a - 2.0f * b + c;
        const float off = den > 1e-12f ? 0.5f * (a - c) / den : 0.0f;
        return (float)t + clampf(off, -0.5f, 0.5f);
    };
    float period = refine(best);
    const int m = (int)(kMaxLag / period);
    if (m >= 2)
    {
        const int around = (int)(period * (float)m + 0.5f);
        int t = around;
        for (int k = around - 2; k <= around + 2; k++)
            if (k > 1 && k <= kMaxLag && d[k] < d[t])
                t = k;
        const float longer = refine(t) / (float)m;
        if (fabsf(longer - period) < 0.5f) // the same tone, not a neighbouring dip
            period = longer;
    }
    const float hz = (float)kRate / period;
    r.pitch = 69.0f + 12.0f * log2f(hz / 440.0f);
    return r;
}

void PitchTracker::begin(Reading* readings, int maxReadings)
{
    out = readings;
    maxOut = maxReadings;
    n = 0;
}

void PitchTracker::feed(const int16_t* pcm, uint32_t frames)
{
    while (n < maxOut && (uint32_t)n * kHop + kNeed <= frames)
    {
        out[n] = analyse(pcm + (uint32_t)n * kHop);
        n++;
    }
}

// ---------------------------------------------------------------- notes ----

void findNotes(const Reading* r, int count, Take& take, Scratch& work)
{
    take.count = 0;
    take.tuning = 0.0f;
    if (count <= 0)
        return;

    // how loud the take is: the loud end (90th percentile) of the levels
    float levels[64];
    int nl = 0;
    const int stride = count / 64 + 1;
    for (int i = 0; i < count && nl < 64; i += stride)
        levels[nl++] = r[i].level;
    for (int i = 1; i < nl; i++)
        for (int j = i; j > 0 && levels[j - 1] > levels[j]; j--)
        {
            float t = levels[j];
            levels[j] = levels[j - 1];
            levels[j - 1] = t;
        }
    float loud = 0.0f;
    for (int i = 0; i < count; i++)
        loud = r[i].level > loud ? r[i].level : loud;
    const float p90 = nl ? levels[(nl * 9) / 10 < nl ? (nl * 9) / 10 : nl - 1] : loud;
    const float ref = p90 > loud * 0.3f ? p90 : loud * 0.3f;
    const float gate = ref * 0.12f > 0.003f ? ref * 0.12f : 0.003f;

    // voiced readings and their pitch (median of 5, filled over tiny gaps)
    float* pitch = work.pitch;
    uint8_t* voiced = work.voiced;
    if (count > kMaxHops)
        count = kMaxHops;
    for (int i = 0; i < count; i++)
    {
        voiced[i] = r[i].level > gate && r[i].clarity >= kVoicedClarity && r[i].pitch >= kMinPitch &&
                    r[i].pitch <= kMaxPitch;
        pitch[i] = voiced[i] ? r[i].pitch : 0.0f;
    }
    // gaps of up to 2 readings inside a whistle: still the whistle
    for (int i = 1; i < count - 1; i++)
    {
        if (voiced[i])
            continue;
        int j = i;
        while (j < count && !voiced[j] && j - i < 3)
            j++;
        if (j < count && voiced[j] && j - i <= 2 && voiced[i - 1] && r[i].level > gate * 0.5f)
        {
            for (int k = i; k < j; k++)
            {
                voiced[k] = 1;
                pitch[k] = pitch[i - 1] + (pitch[j] - pitch[i - 1]) * (float)(k - i + 1) / (float)(j - i + 1);
            }
        }
        i = j - 1 > i ? j - 1 : i;
    }
    // too short to be a note: gone
    for (int i = 0; i < count;)
    {
        if (!voiced[i])
        {
            i++;
            continue;
        }
        int j = i;
        while (j < count && voiced[j])
            j++;
        if (j - i < kMinNoteHops)
            for (int k = i; k < j; k++)
                voiced[k] = 0;
        i = j;
    }
    // smoothed (median of 5 within the whistle: one wrong reading disappears)
    float* smooth = work.smooth;
    for (int i = 0; i < count; i++)
    {
        if (!voiced[i])
        {
            smooth[i] = 0.0f;
            continue;
        }
        float w[5];
        int m = 0;
        for (int k = i - 2; k <= i + 2; k++)
            if (k >= 0 && k < count && voiced[k])
                w[m++] = pitch[k];
        smooth[i] = median(w, m);
    }

    // each whistle (a voiced run) into notes: where the pitch holds still
    for (int i = 0; i < count && take.count < kMaxFound;)
    {
        if (!voiced[i])
        {
            i++;
            continue;
        }
        int runEnd = i;
        while (runEnd < count && voiced[runEnd])
            runEnd++;

        // moving: the pitch changes faster than kMovingSlope
        uint8_t* moving = work.moving;
        for (int k = i; k < runEnd; k++)
        {
            const int a = k - 2 < i ? i : k - 2, b = k + 2 >= runEnd ? runEnd - 1 : k + 2;
            const float slope = b > a ? (smooth[b] - smooth[a]) / ((float)(b - a) * kHopSeconds) : 0.0f;
            moving[k] = fabsf(slope) > kMovingSlope;
        }
        // glides: a stretch where the pitch keeps going one way by more than
        // kGlideSemis at kGlideSlope or faster (a slow slide between notes;
        // a note drifting a little or vibrato never goes that far one way).
        // Found as the legs of a zig-zag that ignores turns under 0.25.
        {
            int legStart = i, ext = i, dir = 0;
            auto markLeg = [&](int a, int b) {
                const float d = smooth[b] - smooth[a];
                const float secs = (float)(b - a) * kHopSeconds;
                if (fabsf(d) < kGlideSemis || secs <= 0.0f || fabsf(d) / secs < kGlideSlope)
                    return;
                // only the part that moves: not the still ends of the leg
                int s0 = a, s1 = b;
                while (s0 < b && fabsf(smooth[s0] - smooth[a]) < 0.3f)
                    s0++;
                while (s1 > s0 && fabsf(smooth[s1] - smooth[b]) < 0.3f)
                    s1--;
                // within it, where it still moves (it can rest on a note on the way)
                for (int q = s0 > a ? s0 - 1 : a; q <= s1 + 1 && q < runEnd; q++)
                {
                    const int qa = q - 3 < a ? a : q - 3, qb = q + 3 > b ? b : q + 3;
                    const float sl = qb > qa ? (smooth[qb] - smooth[qa]) / ((float)(qb - qa) * kHopSeconds) : 0.0f;
                    if (fabsf(sl) >= kGlideSlope * 0.6f)
                        moving[q] = 1;
                }
            };
            for (int k = i + 1; k < runEnd; k++)
            {
                const float p = smooth[k];
                if (dir >= 0 && p > smooth[ext])
                {
                    ext = k;
                    if (dir == 0 && p - smooth[legStart] > 0.25f)
                        dir = 1;
                }
                else if (dir <= 0 && p < smooth[ext])
                {
                    ext = k;
                    if (dir == 0 && smooth[legStart] - p > 0.25f)
                        dir = -1;
                }
                else if ((dir > 0 && p < smooth[ext] - 0.25f) || (dir < 0 && p > smooth[ext] + 0.25f))
                {
                    markLeg(legStart, ext); // turned: the leg so far is done
                    legStart = ext;
                    dir = -dir;
                    ext = k;
                }
            }
            markLeg(legStart, ext);
        }

        // still stretches shorter than kMinStableHops count as moving
        for (int k = i; k < runEnd;)
        {
            if (moving[k])
            {
                k++;
                continue;
            }
            int e = k;
            while (e < runEnd && !moving[e])
                e++;
            if (e - k < kMinStableHops)
                for (int q = k; q < e; q++)
                    moving[q] = 1;
            k = e;
        }

        // the still stretches are the notes
        const int firstOfRun = take.count;
        int lastStillEnd = i;
        for (int k = i; k < runEnd && take.count < kMaxFound;)
        {
            if (moving[k])
            {
                k++;
                continue;
            }
            int e = k;
            while (e < runEnd && !moving[e])
                e++;
            Found f;
            f.pitch = median(smooth + k, e - k);
            f.level = 0.0f;
            for (int q = k; q < e; q++)
                f.level = r[q].level > f.level ? r[q].level : f.level;
            f.slideIn = false;
            if (take.count == firstOfRun)
            {
                f.startHop = i; // the first note starts with the whistle (its scoop included)
            }
            else
            {
                Found& prev = take.note[take.count - 1];
                const int gap = k - lastStillEnd; // the change between the two
                if (fabsf(f.pitch - prev.pitch) < 0.6f)
                {
                    // the same note wobbling: one note
                    prev.endHop = e;
                    prev.level = f.level > prev.level ? f.level : prev.level;
                    lastStillEnd = e;
                    k = e;
                    continue;
                }
                // how long the way from one pitch to the other took: from the
                // last reading near the old pitch to the first near the new
                // one (vibrato stays near; a glide takes its time)
                int t0 = k - 1;
                while (t0 > prev.startHop && fabsf(smooth[t0] - prev.pitch) > 0.35f)
                    t0--;
                int t1 = lastStillEnd > t0 ? lastStillEnd : t0 + 1;
                while (t1 < e - 1 && fabsf(smooth[t1] - f.pitch) > 0.35f)
                    t1++;
                (void)gap;
                if (t1 - t0 >= kSlideHops)
                {
                    f.slideIn = true; // a glide: the note starts where the glide starts
                    f.startHop = t0 + 1;
                }
                else
                {
                    f.startHop = (t0 + t1 + 1) / 2; // a quick change: half way
                }
                prev.endHop = f.startHop;
            }
            f.endHop = e;
            take.note[take.count++] = f;
            lastStillEnd = e;
            k = e;
        }
        if (take.count > firstOfRun)
            take.note[take.count - 1].endHop = runEnd; // the last note ends with the whistle
        i = runEnd;
    }

    // a repeated note within one whistle (the tongue: "tu-tu"): the level
    // drops sharply for a moment and comes straight back. Split there.
    for (int idx = 0; idx < take.count && take.count < kMaxFound; idx++)
    {
        Found& f = take.note[idx];
        for (int h = f.startHop + kMinNoteHops; h < f.endHop - kMinNoteHops; h++)
        {
            float before = 0.0f, after = 0.0f;
            bool lowest = true;
            for (int q = h - 8; q < h; q++)
                before = r[q].level > before ? r[q].level : before;
            for (int q = h + 1; q <= h + 8 && q < count; q++)
                after = r[q].level > after ? r[q].level : after;
            for (int q = h - 3; q <= h + 3; q++)
                lowest = lowest && r[q].level >= r[h].level;
            const float around = before < after ? before : after;
            if (!lowest || r[h].level > around * 0.25f || around < f.level * 0.5f)
                continue;
            for (int q = take.count; q > idx + 1; q--)
                take.note[q] = take.note[q - 1];
            take.count++;
            take.note[idx + 1] = take.note[idx];
            take.note[idx + 1].startHop = h;
            take.note[idx + 1].slideIn = false;
            take.note[idx].endHop = h;
            break; // the second half is looked at next
        }
    }

    // a faint bit right after a note, on the same pitch, is its tail (the
    // whistle fading out near the noise): part of that note
    {
        float lv[kMaxFound];
        for (int k = 0; k < take.count; k++)
            lv[k] = take.note[k].level;
        const float typical = take.count ? median(lv, take.count) : 0.0f;
        int w = 0;
        for (int k = 0; k < take.count; k++)
        {
            Found& f = take.note[k];
            if (w > 0)
            {
                Found& prev = take.note[w - 1];
                const bool samePitch = fabsf(f.pitch - prev.pitch) < 0.6f;
                const bool close = (f.startHop - prev.endHop) * kHopSeconds <= 0.12f;
                if (samePitch && close && f.level < prev.level * 0.7f && f.level < typical * 0.5f)
                {
                    prev.endHop = f.endHop;
                    continue;
                }
            }
            take.note[w++] = f;
        }
        take.count = w;
    }

    // passing notes that are not notes:
    //  - a scoop: the start of a whistle bending up (or down) into its first
    //    real note, less than 1.5 semitones away and over in under 0.17 s
    //  - a fall: the same at the end of a whistle (under 0.12 s)
    //  - a stop on the way: a short note in the middle of a glide that goes on
    //    past it (slides in and out, between its neighbours, under 0.16 s)
    {
        auto secs = [](const Found& f) { return (f.endHop - f.startHop) * kHopSeconds; };
        auto joined = [](const Found& a, const Found& b) { return a.endHop == b.startHop; };
        bool changed = true;
        while (changed)
        {
            changed = false;
            for (int k = 0; k < take.count && !changed; k++)
            {
                Found& f = take.note[k];
                const bool hasPrev = k > 0 && joined(take.note[k - 1], f);
                const bool hasNext = k + 1 < take.count && joined(f, take.note[k + 1]);
                int into = -1; // the neighbour it becomes part of
                if (!hasPrev && hasNext && secs(f) < 0.17f && fabsf(take.note[k + 1].pitch - f.pitch) < 1.5f &&
                    secs(take.note[k + 1]) > secs(f))
                    into = k + 1;
                else if (hasPrev && !hasNext && secs(f) < 0.12f && fabsf(take.note[k - 1].pitch - f.pitch) < 1.5f &&
                         secs(take.note[k - 1]) > secs(f))
                    into = k - 1;
                else if (hasPrev && hasNext && secs(f) < 0.16f && f.slideIn && take.note[k + 1].slideIn)
                {
                    const float a = take.note[k - 1].pitch, c = take.note[k + 1].pitch;
                    if ((f.pitch - a) * (c - f.pitch) > 0.0f) // on the way
                        into = k + 1;
                }
                if (into < 0)
                    continue;
                Found& g = take.note[into];
                if (into > k)
                {
                    g.startHop = f.startHop;
                    g.slideIn = f.slideIn || (into == k + 1 && hasPrev && g.slideIn);
                }
                else
                    g.endHop = f.endHop;
                g.level = f.level > g.level ? f.level : g.level;
                for (int q = k; q + 1 < take.count; q++)
                    take.note[q] = take.note[q + 1];
                take.count--;
                changed = true;
            }
        }
    }

    // the take's tuning: the circular mean of the notes' distance from the
    // nearest semitone, weighted by length
    float sx = 0.0f, sy = 0.0f;
    for (int k = 0; k < take.count; k++)
    {
        const float frac = take.note[k].pitch - floorf(take.note[k].pitch);
        const float w = (float)(take.note[k].endHop - take.note[k].startHop);
        sx += w * cosf(frac * 6.2831853f);
        sy += w * sinf(frac * 6.2831853f);
    }
    if (sx != 0.0f || sy != 0.0f)
    {
        float t = atan2f(sy, sx) / 6.2831853f; // -0.5..0.5
        take.tuning = t;
    }

    // the key: the major or minor scale that holds the most (by length)
    float bestScore = -1.0f;
    for (int key = 0; key < 12; key++)
        for (int minor = 0; minor < 2; minor++)
        {
            float score = 0.0f;
            for (int k = 0; k < take.count; k++)
            {
                const int s = (int)lroundf(take.note[k].pitch - take.tuning);
                const float w = (float)(take.note[k].endHop - take.note[k].startHop);
                if (inScale(s, minor ? kSnapMinor : kSnapMajor, key))
                    score += w;
                // the key's root and fifth a little more likely to be played
                const int pc = ((s - key) % 12 + 12) % 12;
                if (pc == 0)
                    score += w * 0.2f;
                if (pc == 7)
                    score += w * 0.1f;
            }
            if (score > bestScore + 1e-3f)
            {
                bestScore = score;
                take.key = key;
                take.minor = minor != 0;
            }
        }
}

// ---------------------------------------------------------------- grid ----

int snapToScale(float semis, uint8_t snap, int key)
{
    const int nearest = (int)lroundf(semis);
    if (inScale(nearest, snap, key))
        return nearest;
    // the scale note on the side the whistle leaned to, else the other
    const int lean = semis >= (float)nearest ? 1 : -1;
    if (inScale(nearest + lean, snap, key))
        return nearest + lean;
    return nearest - lean;
}

int autoOctaves(const Take& take, int baseNote)
{
    if (take.count == 0)
        return -2;
    float p[kMaxFound];
    for (int k = 0; k < take.count; k++)
        p[k] = take.note[k].pitch;
    const float mid = median(p, take.count);
    // the middle of the take within -6..+6 of the base note
    const int oct = (int)floorf((baseNote + 6.0f - mid) / 12.0f);
    return oct < -5 ? -5 : (oct > 3 ? 3 : oct);
}

int toNotes(const Take& take, const Placement& pl, Note* out, int maxOut)
{
    int n = 0;
    float loud = 1e-6f;
    for (int k = 0; k < take.count; k++)
        loud = take.note[k].level > loud ? take.note[k].level : loud;
    const int grid = pl.gridTicks;
    const float tpH = pl.hopsPerTick > 0.0f ? 1.0f / pl.hopsPerTick : 0.0f; // ticks per reading

    for (int k = 0; k < take.count && n < maxOut; k++)
    {
        const Found& f = take.note[k];
        float s = (float)(f.startHop - pl.anchorHop) * tpH;
        float e = (float)(f.endHop - pl.anchorHop) * tpH;
        int start, end;
        if (grid > 0)
        {
            start = (int)lroundf(s / (float)grid) * grid;
            end = (int)lroundf(e / (float)grid) * grid;
            if (end < start + grid)
                end = start + grid;
        }
        else
        {
            start = (int)lroundf(s);
            end = (int)lroundf(e);
            if (end < start + 12)
                end = start + 12;
        }
        // just before the track starts (whistled a hair early): the start
        if (start < 0 && end > 0 && -start <= (grid > 0 ? grid : 24))
            start = 0;
        if (start < 0 || start >= pl.patternTicks)
            continue;
        if (end > pl.patternTicks)
            end = pl.patternTicks;
        if (end <= start)
            continue;

        int semis = snapToScale(f.pitch - take.tuning, pl.snap, pl.key) + 12 * pl.octaves;
        semis = semis < 0 ? 0 : (semis > 127 ? 127 : semis);
        Note note{};
        note.start = (uint16_t)start;
        note.length = (uint16_t)(end - start);
        note.pitch = (uint8_t)semis;
        const float rel = sqrtf(clampf(f.level / loud, 0.0f, 1.0f));
        note.velocity = (uint8_t)clampf(45.0f + 82.0f * rel, 1.0f, 127.0f);
        note.flags = 0;
        note.chanceInv = 0;

        if (n > 0)
        {
            Note& prev = out[n - 1];
            if (note.start <= prev.start)
            {
                // two on one grid square: the longer one stays
                if (note.length > prev.length)
                    prev = note;
                continue;
            }
            const bool touching = f.slideIn && k > 0 && take.note[k - 1].endHop == f.startHop;
            if (prev.start + prev.length > note.start || (pl.slides && touching))
                prev.length = (uint16_t)(note.start - prev.start); // no overlap; a slide joins them
            if (pl.slides && touching && prev.pitch != note.pitch)
                note.flags |= kNoteFlagSlide;
        }
        out[n++] = note;
    }
    return n;
}

} // namespace whistle
} // namespace gibgob

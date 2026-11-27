// SPDX-License-Identifier: GPL-3.0-only
// GibGob OS PERC: the snare tamer. Plays every kit's snare with the real engine, measures it
// (perc_measure.h) and, where it is harsh or loud, takes its resonance, then its filter drive, then its
// drive, then its level down step by step until it meets the targets. Writes ../PercSnareTame.h (the
// values soundFromKit gives the kits' snares).
//   build (from core/): g++ -std=c++17 -O2 -I libs -I source source/gibgob/perc/tools/perc_tame.cc
//     source/gibgob/perc/Perc.cpp + the libs/lxr C files compiled as C (gibgob-tests/run.sh) -o perc_tame
//   then: ./perc_tame source/gibgob/perc/PercSnareTame.h
#include "perc_measure.h"
#include "gibgob/perc/PercKits.h"
#include <cstdio>

using namespace gibgob::perc;

// the targets: the snare's 2-6 kHz band no louder than this (the kits' cymbals and hats sit around
// -35 .. -41 dB there, their snares around -28), and the snare as a whole under the kicks (about -17)
constexpr double kBandMax = -32.0;
constexpr double kLoudMax = -19.0;

static Engine e;

static double bandLevel(const percmeasure::Result& r)
{
    return r.loudness + 10 * log10(r.harshness + 1e-9);
}

int main(int argc, char** argv)
{
    const char* out = argc > 1 ? argv[1] : "PercSnareTame.h";
    FILE* f = fopen(out, "w");
    if (!f)
        return 1;
    fprintf(f, "// SPDX-License-Identifier: GPL-3.0-only\n// GibGob OS PERC: made by tools/perc_tame.cc; do not edit.\n");
    fprintf(f, "// The kits' snares, tamed: resonance, filter drive, drive and volume each kit's snare gets, so its\n");
    fprintf(f, "// 2-6 kHz band stays under %.0f dB and the whole under %.0f dB (K-weighted, tools/perc_measure.h).\n", kBandMax, kLoudMax);
    fprintf(f, "static constexpr int kSnareTameCount = %d;\nstatic const uint8_t kSnareTame[%d][4] = {\n", lxr::kKitCount, lxr::kKitCount);
    int changed = 0;
    for (int k = 0; k < lxr::kKitCount; k++)
    {
        Sound s;
        soundFromKitData(lxr::kKit[k], kSlotSnare, s);
        const Sound orig = s;
        percmeasure::Result r = percmeasure::measure(e, s);
        const percmeasure::Result before = r;
        for (int guard = 0; guard < 200 && r.peak > 0.003f && (bandLevel(r) > kBandMax || r.loudness > kLoudMax); guard++)
        {
            if (bandLevel(r) > kBandMax && s.value[pFilterReso] > 24)
                s.value[pFilterReso] = (uint8_t)(s.value[pFilterReso] - 8 < 24 ? 24 : s.value[pFilterReso] - 8);
            else if (bandLevel(r) > kBandMax && s.value[pFilterDrive] > 16)
                s.value[pFilterDrive] = (uint8_t)(s.value[pFilterDrive] - 8 < 16 ? 16 : s.value[pFilterDrive] - 8);
            else if (bandLevel(r) > kBandMax && s.value[pDrive] > 0)
                s.value[pDrive] = (uint8_t)(s.value[pDrive] < 8 ? 0 : s.value[pDrive] - 8);
            else if (s.value[pVolume] > 8)
                s.value[pVolume] = (uint8_t)(s.value[pVolume] * 0.9f);
            else
                break;
            r = percmeasure::measure(e, s);
        }
        const bool diff = s.value[pFilterReso] != orig.value[pFilterReso] || s.value[pFilterDrive] != orig.value[pFilterDrive] ||
                          s.value[pDrive] != orig.value[pDrive] || s.value[pVolume] != orig.value[pVolume];
        changed += diff;
        fprintf(f, "    { %3d, %3d, %3d, %3d }, // %-8s band %6.1f -> %6.1f dB, loudness %6.1f -> %6.1f dB\n", s.value[pFilterReso],
                s.value[pFilterDrive], s.value[pDrive], s.value[pVolume], lxr::kKitName[k], bandLevel(before), bandLevel(r),
                before.loudness, r.loudness);
        printf("%-9s reso %3d->%3d fdrv %3d->%3d drv %3d->%3d vol %3d->%3d  band %6.1f->%6.1f  loud %6.1f->%6.1f\n", lxr::kKitName[k],
               orig.value[pFilterReso], s.value[pFilterReso], orig.value[pFilterDrive], s.value[pFilterDrive], orig.value[pDrive],
               s.value[pDrive], orig.value[pVolume], s.value[pVolume], bandLevel(before), bandLevel(r), before.loudness, r.loudness);
    }
    fprintf(f, "};\n");
    fclose(f);
    printf("%d of %d snares tamed\n", changed, lxr::kKitCount);
    return 0;
}

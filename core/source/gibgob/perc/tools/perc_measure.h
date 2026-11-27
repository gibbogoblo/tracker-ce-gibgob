// SPDX-License-Identifier: GPL-3.0-only
// GibGob OS PERC: how loud and how harsh a hit is (the snare tamer, tools/perc_tame.cc, and the tests).
//  loudness: K-weighted (ITU-R BS.1770: the high shelf and the 38 Hz high-pass) mean square over the
//            hit's first 300 ms, in dB (-0.691 as LUFS); not gated, a single hit
//  harshness: the share of that K-weighted energy in the 2-6 kHz band (a band-pass at 3.5 kHz, Q 0.9),
//            where the ear is most sensitive and a resonant or overdriven noise snare piles up
#pragma once
#include "gibgob/perc/Perc.h"
#include "lxr/Soundengine/DSPAudio/random.h"
#include <cmath>
#include <vector>

namespace percmeasure
{
struct Biquad
{
    double b0, b1, b2, a1, a2, z1 = 0, z2 = 0;
    double run(double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};
// the RBJ cookbook's filters
inline Biquad norm(double b0, double b1, double b2, double a0, double a1, double a2)
{
    Biquad q;
    q.b0 = b0 / a0; q.b1 = b1 / a0; q.b2 = b2 / a0; q.a1 = a1 / a0; q.a2 = a2 / a0;
    return q;
}
inline Biquad highShelf(double fs, double f0, double dB, double Q)
{
    const double A = pow(10, dB / 40), w = 2 * M_PI * f0 / fs, c = cos(w), al = sin(w) / (2 * Q), sA = 2 * sqrt(A) * al;
    return norm(A * ((A + 1) + (A - 1) * c + sA), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sA),
                (A + 1) - (A - 1) * c + sA, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sA);
}
inline Biquad highPass(double fs, double f0, double Q)
{
    const double w = 2 * M_PI * f0 / fs, c = cos(w), al = sin(w) / (2 * Q);
    return norm((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
}
inline Biquad bandPass(double fs, double f0, double Q)
{
    const double w = 2 * M_PI * f0 / fs, c = cos(w), al = sin(w) / (2 * Q);
    return norm(al, 0, -al, 1 + al, -2 * c, 1 - al);
}

struct Result
{
    double loudness;  // dB
    double harshness; // 0..1
    float peak;
};

// one hit of the sound at velocity 100, its own pitch
inline Result measure(gibgob::perc::Engine& e, const gibgob::perc::Sound& s)
{
    const int n = 44100 * 3 / 10 / 64 * 64;
    std::vector<float> l(n, 0.f), r(n, 0.f);
    e.init();
    e.setSound(0, s);
    lxr_seedRandom(0x2545F491u); // the same noise every time: the tamer and the tests agree
    e.trigger(0, 100, 60);
    e.render(l.data(), r.data(), n);
    Biquad shelf = highShelf(44100, 1681.974450955533, 3.999843853973347, 0.7071752369554196);
    Biquad hp = highPass(44100, 38.13547087602444, 0.5003270373238773);
    Biquad bp = bandPass(44100, 3500, 0.9);
    double ek = 0, eb = 0;
    float peak = 0;
    for (int i = 0; i < n; i++)
    {
        const double x = 0.5 * (l[i] + r[i]);
        peak = fmaxf(peak, fabsf((float)x));
        const double k = hp.run(shelf.run(x));
        const double b = bp.run(k);
        ek += k * k;
        eb += b * b;
    }
    Result res;
    res.loudness = -0.691 + 10 * log10(ek / n + 1e-15);
    res.harshness = ek > 0 ? eb / ek : 0;
    res.peak = peak;
    return res;
}
} // namespace percmeasure

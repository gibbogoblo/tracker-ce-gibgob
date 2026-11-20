#!/usr/bin/env python3
# GORILLAGRAIN's own sample: a warm, slowly changing "hoo - oh - aah - oh"
# choir tone at C4 (the note it plays as recorded), made for grains: every
# part of it is in tune and a little different. Writes GORILLA.wav
# (44.1 kHz, mono, 16-bit).   python3 make_sample.py <out dir>
import math, os, sys, wave
import numpy as np

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))
SR = 44100
SECONDS = 6.0
F0 = 261.6256  # C4

# vowel formants (Hz, bandwidth Hz, gain)
VOWELS = {
    'oo': [(300, 80, 1.0), (870, 100, 0.25), (2240, 140, 0.05)],
    'oh': [(450, 90, 1.0), (800, 100, 0.55), (2830, 150, 0.08)],
    'ah': [(730, 100, 1.0), (1090, 110, 0.6), (2440, 160, 0.15)],
}
PATH = [(0.0, 'oo'), (1.6, 'oh'), (3.2, 'ah'), (4.6, 'oh'), (6.0, 'oo')]


def formants_at(t):
    for (t0, v0), (t1, v1) in zip(PATH, PATH[1:]):
        if t0 <= t <= t1:
            x = (t - t0) / (t1 - t0)
            x = x * x * (3 - 2 * x)
            return [tuple(a * (1 - x) + b * x for a, b in zip(f0, f1)) for f0, f1 in zip(VOWELS[v0], VOWELS[v1])]
    return VOWELS['oo']


def main():
    n = int(SR * SECONDS)
    t = np.arange(n) / SR
    rng = np.random.default_rng(62)
    # three singers a little apart: a choir, not one voice
    out = np.zeros(n)
    hop = 256
    frames = range(0, n, hop)
    for k, (detune, vib_rate, vib_depth, pan_gain) in enumerate([(0.0, 5.1, 0.12, 1.0), (+0.07, 4.7, 0.10, 0.8), (-0.06, 5.5, 0.11, 0.8)]):
        # vibrato fades in (cents -> ratio), slow drift
        vib = vib_depth * np.minimum(1.0, t / 1.2) * np.sin(2 * math.pi * vib_rate * t + k)
        drift = 0.04 * np.sin(2 * math.pi * 0.13 * t + 2 * k)
        semis = detune + vib + drift
        freq = F0 * 2 ** (semis / 12.0)
        phase = 2 * math.pi * np.cumsum(freq) / SR
        voice = np.zeros(n)
        for h in range(1, 40):
            if F0 * h > 9000:
                break
            # the harmonic's level, frame by frame, from the formants (smoothly)
            amp = np.zeros(len(frames))
            for i, s in enumerate(frames):
                fh = F0 * h
                a = 0.0
                for (fc, bw, g) in formants_at(s / SR):
                    a += g / (1 + ((fh - fc) / bw) ** 2)
                amp[i] = a / h ** 0.6
            amp = np.interp(np.arange(n), np.array(list(frames)), amp)
            voice += amp * np.sin(h * phase + rng.uniform(0, 2 * math.pi))
        out += voice * pan_gain
    # breath: soft noise around 1.4 kHz
    noise = rng.standard_normal(n)
    spec = np.fft.rfft(noise)
    f = np.fft.rfftfreq(n, 1 / SR)
    spec *= np.exp(-((f - 1400) / 900) ** 2)
    breath = np.fft.irfft(spec, n)
    breath /= np.max(np.abs(breath))
    out /= np.max(np.abs(out))
    out += 0.035 * breath
    # gentle swells, 30 ms fades at the ends
    out *= 0.85 + 0.15 * np.sin(2 * math.pi * 0.33 * t - 1.0)
    fade = int(0.03 * SR)
    out[:fade] *= np.linspace(0, 1, fade)
    out[-fade:] *= np.linspace(1, 0, fade)
    out *= 0.89 / np.max(np.abs(out))  # -1 dBFS
    pcm = (out * 32767).astype('<i2')
    path = os.path.join(OUT, 'GORILLA.wav')
    with wave.open(path, 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm.tobytes())
    print('wrote', path, len(pcm), 'frames')


if __name__ == '__main__':
    main()

/* Audio Library for Teensy 3.X
 * Copyright (c) 2014, Paul Stoffregen, paul@pjrc.com
 *
 * Development of this audio library was funded by PJRC.COM, LLC by sales of
 * Teensy and Audio Adaptor boards.  Please support PJRC's efforts to develop
 * open source software by purchasing Teensy or other PJRC products.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice, development funding notice, and this permission
 * notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef audio_mixer_h_
#define audio_mixer_h_

#include "AudioStream.h"
#include "dspApplyGain.h"
#include "util_math.h"

#ifdef AUDIO_NULL_IS_SILENCE
class AudioMixerBase : public AudioStream
{
  protected:
    AudioMixerBase(unsigned char inputs, audio_block_t** queue)
      : AudioStream(inputs, queue)
    {
    }
    void mix(int size, int32_t* multiplier, int32_t* previousMultiplier, SaturationType saturationType);

  public:
    virtual bool isInputSilenced(unsigned int channel) const
    {
        (void)channel;
        return false;
    }
};
#define AUDIO_MIXER_PARENT AudioMixerBase
#else
#define AUDIO_MIXER_PARENT AudioStream
#endif

template<int mixerSize>
class AudioMixer : public AUDIO_MIXER_PARENT
{
  public:
    static constexpr float kUnityGain = 65536.0f;
    static constexpr float kMinGain = -32767.0f;
    static constexpr float kMaxGain = 32767.0f;

    AudioMixer(void)
      : AUDIO_MIXER_PARENT(mixerSize, inputQueueArray)
    {
        for (int i = 0; i < mixerSize; i++)
        {
            multiplier[i] = (int32_t)kUnityGain;
            previousMultiplier[i] = (int32_t)kUnityGain;
        }

        setSaturationType(SaturationType::kHardClip);
    }
    virtual void update(void);
    void gain(unsigned int channel, float gain)
    {
        if (channel >= mixerSize)
        {
            return;
        }

        gain = clamp<float>(gain, kMinGain, kMaxGain);

        previousMultiplier[channel] = multiplier[channel];
        multiplier[channel] = gain * kUnityGain;
    }

    void setSaturationType(SaturationType type) { saturationType = type; }

#ifdef AUDIO_NULL_IS_SILENCE
    bool isInputSilenced(unsigned int channel) const override
    {
        return channel < mixerSize && multiplier[channel] == 0 && previousMultiplier[channel] == 0;
    }
#endif

  protected:
    SaturationType saturationType;
    int32_t multiplier[mixerSize];
    int32_t previousMultiplier[mixerSize];

  private:
    audio_block_t* inputQueueArray[mixerSize];
};

#ifdef AUDIO_NULL_IS_SILENCE
// GibGob OS: the tracks mixer with the kick duck. Every input not in the
// exempt mask is scaled by the duck gain (on top of its own gain, which holds
// the panning), ramped over the block like any gain change.
extern volatile int32_t g_gibgobDuckQ16;   // 65536 = no duck
extern volatile uint32_t g_gibgobDuckExempt; // inputs left alone (the kick track's)

template<int mixerSize>
class AudioMixerDuck : public AudioMixer<mixerSize>
{
  public:
    AudioMixerDuck()
    {
        for (int i = 0; i < mixerSize; i++)
            effPrev[i] = (int32_t)AudioMixer<mixerSize>::kUnityGain;
    }
    AUDIO_CORE_RAM virtual void update(void)
    {
        const int32_t duck = g_gibgobDuckQ16;
        const uint32_t exempt = g_gibgobDuckExempt;
        for (int i = 0; i < mixerSize; i++)
        {
            const int32_t base = this->multiplier[i];
            eff[i] = (((exempt >> i) & 1) || duck >= 65536) ? base : (int32_t)(((int64_t)base * duck) >> 16);
        }
        this->mix(mixerSize, eff, effPrev, this->saturationType);
        // the mixer's own gains have been reached (the ramp ran on eff)
        for (int i = 0; i < mixerSize; i++)
            this->previousMultiplier[i] = this->multiplier[i];
    }
#ifdef AUDIO_NULL_IS_SILENCE
    bool isInputSilenced(unsigned int channel) const override
    {
        return channel < mixerSize && this->multiplier[channel] == 0 && effPrev[channel] == 0;
    }
#endif

  private:
    int32_t eff[mixerSize];
    int32_t effPrev[mixerSize];
};
#endif

#endif

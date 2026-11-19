#include "GibGobPerformNode.h"

#include "gibgob/Master.h"
#include "gibgob/Perform.h"
#include "config.h"
#include <string.h>

AudioEffectGibGobPerform& gibgobPerformNode()
{
    static AudioEffectGibGobPerform node;
    return node;
}

AUDIO_CORE_RAM void AudioEffectGibGobPerform::update(void)
{
    audio_block_t* left = receiveWritable(0);
    audio_block_t* right = receiveWritable(1);

    gibgob::perform::Engine& engine = gibgob::perform::engine();
    if (!engine.ready())
    {
        if (left)
        {
            transmit(left, 0);
            release(left);
        }
        if (right)
        {
            transmit(right, 1);
            release(right);
        }
        return;
    }

    // a missing block is silence: the effects still run (tails, recording)
    if (left == nullptr)
    {
        left = allocate();
        if (left)
            memset(left->data, 0, sizeof(left->data));
    }
    if (right == nullptr)
    {
        right = allocate();
        if (right)
            memset(right->data, 0, sizeof(right->data));
    }
    if (left == nullptr || right == nullptr)
    {
        if (left)
        {
            transmit(left, 0);
            release(left);
        }
        if (right)
        {
            transmit(right, 1);
            release(right);
        }
        return;
    }

    // kept off the interrupt stack (only the audio interrupt uses them)
    static float l[AUDIO_BLOCK_SAMPLES], r[AUDIO_BLOCK_SAMPLES];
    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
    {
        l[i] = (float)left->data[i] * (1.0f / 32768.0f);
        r[i] = (float)right->data[i] * (1.0f / 32768.0f);
    }
    engine.process(l, r, AUDIO_BLOCK_SAMPLES, gibgob::perform::currentClock());
    // then the master effects (colour, glue) and the duck for the next block
    gibgob::master::audioBlock(l, r, AUDIO_BLOCK_SAMPLES);
    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
    {
        float a = l[i] * 32768.0f, b = r[i] * 32768.0f;
        a = a > 32767.0f ? 32767.0f : (a < -32768.0f ? -32768.0f : a);
        b = b > 32767.0f ? 32767.0f : (b < -32768.0f ? -32768.0f : b);
        left->data[i] = (int16_t)a;
        right->data[i] = (int16_t)b;
    }
    transmit(left, 0);
    transmit(right, 1);
    release(left);
    release(right);
}

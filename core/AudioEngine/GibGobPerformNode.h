#ifndef GIBGOB_PERFORM_NODE_H_
#define GIBGOB_PERFORM_NODE_H_

// GibGob OS: the performance effects on the master mix, between the main
// mixer (tracks, synths, sends) and the output limiter.

#include "AudioStream.h"

class AudioEffectGibGobPerform : public AudioStream
{
  public:
    AudioEffectGibGobPerform()
      : AudioStream(2, inputQueueArray)
    {
    }
    virtual void update(void);

  private:
    audio_block_t* inputQueueArray[2];
};

// Created the first time it is asked for. The audio engine wires its graph in
// its constructor, during static initialisation, so the node must exist by
// then (a plain global could be constructed after the engine, wiping its
// connections).
AudioEffectGibGobPerform& gibgobPerformNode();

#endif

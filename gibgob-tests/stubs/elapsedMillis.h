#pragma once
#include <stdint.h>
inline uint32_t g_testMillis = 0; // the tests move the clock
class elapsedMillis
{
  public:
    operator uint32_t() const { return g_testMillis; }
};

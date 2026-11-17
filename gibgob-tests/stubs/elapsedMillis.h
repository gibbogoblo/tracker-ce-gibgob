#pragma once
#include <stdint.h>
class elapsedMillis
{
  public:
    operator uint32_t() const { return 0; }
};

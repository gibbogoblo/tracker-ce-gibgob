#pragma once
#include "core/modulesBase.h"
struct Platform { Buttons b; Buttons* getButtons() { return &b; } };
struct Interface { Platform p; Platform* getPlatform() { return &p; } };
extern Interface mtInterface;

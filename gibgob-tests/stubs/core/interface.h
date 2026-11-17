#pragma once
#include "core/modulesBase.h"
struct Platform { Buttons b; Buttons* getButtons() { return &b; } };
struct Interface { Platform p; Platform* getPlatform() { return &p; } void deactivateModule(ModuleBase*) {} void activateModule(ModuleBase*, unsigned) {} };
extern Interface mtInterface;

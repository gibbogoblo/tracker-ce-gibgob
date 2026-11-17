#pragma once
#include <stdint.h>
class InputProcessor { public: void setButtonObj(uint8_t, uint8_t, void (*)(uint8_t)) {} void blockAllInputsExcept(uint8_t) {} void unblockAllInputs() {} };
class ModuleBase { public: virtual ~ModuleBase() {} };
struct Buttons { bool isButtonPressed(int) { return false; } };

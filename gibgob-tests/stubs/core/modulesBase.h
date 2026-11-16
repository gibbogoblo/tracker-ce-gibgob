#pragma once
#include <stdint.h>
class InputProcessor { public: void setButtonObj(uint8_t, uint8_t, void (*)(uint8_t)) {} };
struct Buttons { bool isButtonPressed(int) { return false; } };

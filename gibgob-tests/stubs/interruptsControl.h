#pragma once
void testDisableIrq();
void testEnableIrq();
#define DISABLE_AUDIO_INTERRUPTS() testDisableIrq()
#define ENABLE_AUDIO_INTERRUPTS() testEnableIrq()

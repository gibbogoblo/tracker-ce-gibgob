#pragma once
#include <stdint.h>
typedef int8_t hControl;
enum control_id_t { controlIdGibGobPianoRoll, controlIdGibGobTrackGrid, controlIdGibGobTable, controlIdGibGobDrumSeq, controlIdGibGobPerform };
struct strControlProperties { const char* text = nullptr; int value = 0; void* data = nullptr; uint32_t style = 0; const uint32_t* colors = nullptr; uint16_t x = 0, y = 0, w = 0, h = 0; };
class cDisplayControl
{
  public:
    virtual void update() = 0;
    virtual void setStyle(uint32_t style) = 0;
    virtual void setText(const char* text) = 0;
    virtual void setValue(int value) = 0;
    virtual void setColors(const uint32_t* colors) = 0;
    virtual void setData(void* data) = 0;
    cDisplayControl(control_id_t, uint8_t) {}
    virtual ~cDisplayControl() {}
  protected:
    uint32_t style = 0; const char* text = nullptr; int value = 0;
    uint16_t posX = 0, posY = 0, width = 0, height = 0; const uint32_t* colors = nullptr;
};

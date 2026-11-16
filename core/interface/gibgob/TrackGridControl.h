#ifndef GIBGOB_TRACKGRIDCONTROL_H_
#define GIBGOB_TRACKGRIDCONTROL_H_

#include "displayControls.h"

namespace gibgob
{

struct TrackTile
{
    char title[24];  // "1  Kick"
    char type[12];   // "DRUM" / "MELODIC"
    char sound[40];  // instrument name
    char detail[32]; // "12 steps" / "8 notes"
    bool melodic;
    bool muted;
    bool active;     // currently sounding / has content at playhead
};

struct TrackGridView
{
    TrackTile tiles[8];
    int cursor = 0;
    char info[96] = { 0 };
};

class TrackGridControl : public cDisplayControl
{
  public:
    TrackGridControl(strControlProperties* properties = nullptr);
    virtual ~TrackGridControl() {}

    virtual void update();
    virtual void setStyle(uint32_t style) { this->style = style; }
    virtual void setText(const char* text) { this->text = text; }
    virtual void setValue(int value) { this->value = value; }
    virtual void setColors(const uint32_t* colors) { this->colors = colors; }
    virtual void setData(void* data) { view = (TrackGridView*)data; }

  private:
    TrackGridView* view = nullptr;
};

} // namespace gibgob

#endif

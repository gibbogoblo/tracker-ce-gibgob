#ifndef GIBGOB_PERFORMCONTROL_H_
#define GIBGOB_PERFORMCONTROL_H_

#include "displayControls.h"
#include "gibgob/gibgobUi.h"

namespace gibgob
{

// The Perform page: a 2 x 4 grid, one cell per top-row button.
struct PerformView
{
    struct CellView
    {
        char name[16];
        char line[3][24];
        uint32_t color;
        bool active; // held / sounding
        bool off;
    };
    CellView cells[8];
    int cursor = 0;

    char title[32] = { 0 };
    ParamBox params[4];
    int selectedParam = 0;
    bool shift = false;
    BottomBar bar;
    HelpView help;
};

class PerformControl : public cDisplayControl
{
  public:
    PerformControl(strControlProperties* properties = nullptr);
    virtual ~PerformControl() {}

    virtual void update();
    virtual void setStyle(uint32_t style) { this->style = style; }
    virtual void setText(const char* text) { this->text = text; }
    virtual void setValue(int value) { this->value = value; }
    virtual void setColors(const uint32_t* colors) { this->colors = colors; }
    virtual void setData(void* data) { view = (PerformView*)data; }

  private:
    PerformView* view = nullptr;
};

// colour of an effect (cells, bottom bar)
uint32_t performFxColor(int fx);

} // namespace gibgob

#endif

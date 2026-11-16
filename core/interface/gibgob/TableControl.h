#ifndef GIBGOB_TABLECONTROL_H_
#define GIBGOB_TABLECONTROL_H_

#include "displayControls.h"
#include "gibgob/gibgobUi.h"

namespace gibgob
{

// A simple table of text cells with a cursor, used by the settings pages.
struct TableView
{
    static constexpr int kMaxRows = 12;
    static constexpr int kMaxCols = 5;

    int rows = 0;
    int cols = 0;
    int colX[kMaxCols] = { 0 };
    const char* header[kMaxCols] = { nullptr }; // nullptr in [0] = no header row
    char cell[kMaxRows][kMaxCols][20];
    uint32_t rowColor[kMaxRows] = { 0 }; // accent of the row's cursor, 0 = white
    int cursorRow = 0;
    int cursorCol = 0;

    char title[56] = { 0 };
    char mode[12] = { 0 };
    uint32_t modeColor = 0;
    bool shift = false;
    BottomBar bar;
};

class TableControl : public cDisplayControl
{
  public:
    TableControl(strControlProperties* properties = nullptr);
    virtual ~TableControl() {}

    virtual void update();
    virtual void setStyle(uint32_t style) { this->style = style; }
    virtual void setText(const char* text) { this->text = text; }
    virtual void setValue(int value) { this->value = value; }
    virtual void setColors(const uint32_t* colors) { this->colors = colors; }
    virtual void setData(void* data) { view = (TableView*)data; }

  private:
    TableView* view = nullptr;
};

} // namespace gibgob

#endif

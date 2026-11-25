#ifndef GIBGOB_GAMEMENUCONTROL_H_
#define GIBGOB_GAMEMENUCONTROL_H_

// GibGob OS: the Menu screen (Menu held), a video game style menu on a beach:
// three items at a time, the one you stand on in the middle on a highlighted
// band, and the monkey in the corner telling you what it is.

#include "displayControls.h"
#include "gibgob/gibgobUi.h"

namespace gibgob
{

struct GameMenuView
{
    static constexpr int kMaxItems = 16;
    int count = 0;
    const char* items[kMaxItems] = { nullptr };       // the names (drawn in capitals)
    const char* descriptions[kMaxItems] = { nullptr }; // what the monkey says
    int selected = 0;
    // the highlighted item's modes (e.g. Master's 4 pages): dots, the current filled
    int modeCount = 0;
    int modeCurrent = 0;
};

// the item `offset` places from `selected`, the list going round
int gameMenuItemAt(const GameMenuView& v, int offset);
// the description in lines of at most `width` letters (returns how many, <= maxLines)
int gameMenuWrap(const char* text, int width, char lines[][48], int maxLines);

class GameMenuControl : public cDisplayControl
{
  public:
    GameMenuControl(strControlProperties* properties = nullptr);
    virtual ~GameMenuControl() {}

    virtual void update();
    virtual void setStyle(uint32_t style) { this->style = style; }
    virtual void setText(const char* text) { this->text = text; }
    virtual void setValue(int value) { this->value = value; }
    virtual void setColors(const uint32_t* colors) { this->colors = colors; }
    virtual void setData(void* data) { view = (GameMenuView*)data; }

  private:
    GameMenuView* view = nullptr;
};

} // namespace gibgob

#endif

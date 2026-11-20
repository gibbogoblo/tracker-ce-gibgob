#ifndef GIBGOB_LIBRARY_PAGE_H_
#define GIBGOB_LIBRARY_PAGE_H_

#include "core/modulesBase.h"
#include "gibgob/TableControl.h"

// GibGob OS sample library (track assign): every WAV in /Instruments,
// /Samples and /Recordings*, by genre and category, with favourites and the
// recently used. Opened with Middle on a track's Sound row.
//   top 1-8      : genres (All .. Mine, Favourites, Recent), then categories
//   Shift        : back to the genres
//   Up/Down, +/- : move (each sample plays)   Left/Right: a page
//   Middle       : load it onto the track
//   hold Note / Instr + +/- : its genre / category    FX1: favourite
//   FX2          : the folders (the file browser)    Button 1: back
class cGibGobLibrary : public ModuleBase
{
  public:
    virtual void update();
    virtual void start(uint32_t options);
    virtual void stop();
    virtual void initDisplayControls();
    virtual void destroyDisplayControls() {}

    static uint32_t makeOptions(int track, int origin) { return ((uint32_t)origin << 4) | (uint32_t)(track & 7); }

    void setDefaultScreenFunct();
    void refresh();
    void refilter();
    void move(int delta);
    void topButton(int b);
    void changeTag(int delta);
    void assign();
    void back(bool folders);

    int track = 0;
    int origin = 0;
    hControl table = -1;
    char titleText[32];
};

extern cGibGobLibrary gibgobLibrary;

#endif

#ifndef GIBGOB_PAGES_H_
#define GIBGOB_PAGES_H_

#include "core/modulesBase.h"
#include "gibgob/TableControl.h"

// GibGob OS settings pages: one module, two pages.
//   Track page   : type, sound (load samples), base note, length
//   Chord palette: the 16 chords on the top-row buttons (2 banks of 8);
//                  shown as the 3rd page of the synth parameters
class cGibGobPages : public ModuleBase
{
  public:
    enum Page : uint8_t
    {
        kPageTrack = 0,
        kPagePalette = 1,
    };
    enum Origin : uint8_t
    {
        kFromHome = 0,
        kFromPianoRoll = 1,
        kFromInstrumentEditor = 2, // chord palette as the 3rd synth-parameters page
    };
    static uint32_t makeOptions(Page page, Origin origin, int track)
    {
        return ((uint32_t)page << 8) | ((uint32_t)origin << 4) | (uint32_t)(track & 7);
    }

    virtual void update();
    virtual void start(uint32_t options);
    virtual void stop();
    virtual void initDisplayControls();
    virtual void destroyDisplayControls() {}

    void setDefaultScreenFunct();
    void refresh();

    void moveCursor(int dCol, int dRow);
    void changeValue(int delta, bool big);
    void topButton(int button, bool pressed);
    void goBack();
    void switchPage(Page newPage);
    void setFeelMode(int mode); // the Chord Canopy: plain chord, strum, arp or human
    void pressMiddle();
    void loadSample();  // the folder browser
    void openLibrary(); // the sample library (Middle on Sound)
    void prepareImport();
    // the library hands back: load this file onto the track / open the folders
    static void queueImport(const char* fullPath);
    static void queueFolderBrowser();
    void backToInstrumentEditor();

    Page page = kPageTrack;
    Origin origin = kFromHome;
    int track = 0;
    int row = 0;
    int col = 1;
    bool auditioning = false;

    gibgob::TableView view;
    hControl table = -1;
    char titleText[40];
};

extern cGibGobPages gibgobPages;

#endif

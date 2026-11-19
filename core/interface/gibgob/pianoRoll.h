#ifndef GIBGOB_PIANOROLL_H_
#define GIBGOB_PIANOROLL_H_

#include "core/modulesBase.h"
#include "gibgob/PianoRollControl.h"

class cPianoRoll : public ModuleBase
{
  public:
    enum Param : uint8_t
    {
        kParamProb = 0,  // Note button  (green)
        kParamVelocity,  // Instr button (yellow)
        kParamNudge,     // FX1 button   (purple)
        kParamInversion, // FX2 button   (light blue)
        kParamCount
    };

    enum class Mode : uint8_t
    {
        Idle,    // d-pad moves the ghost cursor
        BoxDrag, // Insert pressed once: dragging a selection box
        Move,    // notes selected: d-pad moves them
    };

    enum class Menu : uint8_t
    {
        None,
        Quantize, // Shift + Note
        Acid,     // Shift + Instr
        Picker,   // note picker, opened from the Acid menu
        Arp,      // Shift + FX1
    };

    virtual void update();
    virtual void start(uint32_t options); // options = track index
    virtual void stop();
    virtual void initDisplayControls();
    virtual void destroyDisplayControls() {}

    void setDefaultScreenFunct();
    void refreshView();

    // actions
    void pressUp();
    void pressDown();
    void pressLeft();
    void pressRight();
    void pressMiddle();
    bool assignSliceToHeld(); // slice track: held top button + Middle
    void pressInsert();
    void pressDelete();
    void toggleShift();
    void changeParam(int delta);
    void changeLength(int deltaGrid);
    void changeZoom(int delta);
    void topButton(int button, bool pressed);
    void releaseAllButtons();
    void auditionGhost(bool on);
    void openTrackPage();
    void utility(int button); // Shift + top row
    void copyOrPaste();

    // helpers
    void moveCursor(int dTicks, int dPitch);
    void moveSelection(int dTicks, int dPitch);
    void placeGhost();
    int selectUnderCursor();
    int selectInBox();
    void clearSelection();
    int selectionCount();
    bool padsMode(); // sliced sample: the top row plays slices instead of chords
    int chordNoteCount();
    int findEditNote(int tick, int pitch); // note under the cursor (start within +-10, or covering)

    // menus
    void openMenu(Menu m);
    void closeMenu(bool apply);
    void refreshMenu();
    void menuMove(int dCol, int dRow);
    void menuChange(int delta, bool big);
    void menuMiddle();
    void menuTopButton(int button);
    void pickerToggle();

    Menu menu = Menu::None;
    int quantCol = 0;            // 0 = Quantize, 1 = Humanize
    int quantAmount[2] = { 10, 4 };
    gibgob::AcidOptions acid;
    gibgob::TrackSettings arpBackup;
    uint8_t pickerMask[16] = { 0 };

    int track = 0;
    int chordSlot = -1; // palette slot of the ghost chord, -1 = single note
    int inversion = 0;
    int velocity = 100;
    int chance = 100; // % for new notes
    int nudge = 0;
    int lengthTicks = 48;
    int zoom = 0;
    uint8_t param = kParamProb;
    bool paramHeld = false; // a coloured button is held: +/- change `param`
    Mode mode = Mode::Idle;
    bool autoAudition = false;
    uint32_t auditionOffAt = 0;
    int lastPlayheadDrawn = -2;

    bool held[8] = { false };
    bool heldRecording[8] = { false };
    uint8_t heldNotes[8][gibgob::kMaxChordNotes];
    uint8_t heldCount[8] = { 0 };

    gibgob::PianoRollView view;
    hControl grid = -1;
    char titleText[40];
    char barText[8][16];
};

extern cPianoRoll pianoRoll;

#endif

#ifndef GIBGOB_HELP_H_
#define GIBGOB_HELP_H_

// The shortcut lists shown by Shift + Menu on each GibGob screen.
// Add every new button combination here.

namespace gibgob
{

static const char* const kHelpMain[] = {
    "D-pad: move.  Left from step 1: track name",
    "Middle on name: track settings",
    "Middle on drum step: step on / off",
    "Middle on melodic lane: piano roll",
    "Insert: open the track's sequencer",
    "Delete: delete the step",
    "Top row: play tracks (record with Rec)",
    "Note/Instr/FX1/FX2: Len/Vel/Nudge/Pitch",
    "Big +/-: step length (Len: 16..128)",
    "On name: +/- sound, big +/- type",
    "Rec: live rec (stopped: 1 bar count-in)",
    "Play: play / stop",
    "Button 2: this track's sound parameters",
    "Shift+Rec: undo",
    "Shift+Play: play song",
    "Shift+Insert: duplicate track",
    "Shift+Delete: clear track",
    "Shift+top row: mute / unmute",
    "Shift+Up/Down: previous / next pattern",
    "Shift+Left/Right: 4 steps",
    "Shift+FX1: BPM     Shift+FX2: swing",
    "Shift+small +/-: change sound",
    "Shift+big +/-: zoom",
    "Hold Shift + +/-: master volume",
    "Button 1: back here from any screen",
};

static const char* const kHelpPianoRoll[] = {
    "D-pad: move the ghost note",
    "Middle: place note / grab note / let go",
    "Insert: box select (again: lock, again: off)",
    "Delete: delete note(s) under the cursor",
    "Delete on empty: ghost back to one note",
    "Top 1: single note   Top 2-8: chords",
    "Note/Instr/FX1/FX2: Prob/Vel/Nudge/Inv",
    "Small +/-: edit note under the cursor",
    "Big +/-: note length",
    "Rec: live rec (stopped: 1 bar count-in)",
    "Button 1: main sequencer",
    "Button 2: this track's sound parameters",
    "Shift+Rec: undo",
    "Shift+Delete: clear track",
    "Shift+Note: quantize / humanize",
    "Shift+Instr: acid generator",
    "Shift+FX1: arp + Go Ape",
    "Shift+Middle: auto audition",
    "Shift+Up/Down: octave",
    "Shift+Left/Right: 4 steps",
    "Shift+big +/-: zoom",
    "Shift+top: Oct-/Oct+/Semi-/Semi+",
    "   Copy/Paste/Double/Legato",
    "Hold Shift + +/-: master volume",
};

static const char* const kHelpDrum[] = {
    "Top row: steps of this row on / off",
    "Up/Down: row of 8 steps",
    "Left/Right: step (for its values)",
    "Middle: step on / off",
    "Delete: delete the step",
    "Note/Instr/FX1/FX2: Prob/Vel/Nudge/Pitch",
    "Small +/-: edit the step's value",
    "Big +/-: step length",
    "Rec: live rec (stopped: 1 bar count-in)",
    "Button 1: main sequencer",
    "Button 2: this track's sound parameters",
    "Shift+Rec: undo",
    "Shift+Delete: clear track",
    "Shift+Play: play song",
    "Hold Shift + +/-: master volume",
};

static const char* const kHelpTrackPage[] = {
    "Up/Down: setting",
    "Small +/-: change    Big +/-: bigger steps",
    "Middle on Sound: load a sample",
    "Top row: choose the track",
    "Length, big +/-: 16/32/48/64/128",
    "Button 1: back",
    "Button 2: this track's sound parameters",
};

static const char* const kHelpPalette[] = {
    "Up/Down: chord    Left/Right: column",
    "Small +/-: change    Big +/-: octave / jump",
    "Top 2-8: hear that chord",
    "Button 2: back to the sound parameters",
    "Button 1: main sequencer",
};

#define GIBGOB_HELP(arr) (arr), (int)(sizeof(arr) / sizeof((arr)[0]))

} // namespace gibgob

#endif

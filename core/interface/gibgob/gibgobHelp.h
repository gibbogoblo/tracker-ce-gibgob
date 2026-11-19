#ifndef GIBGOB_HELP_H_
#define GIBGOB_HELP_H_

// The shortcut lists shown by Shift + Menu on each GibGob screen.
// Add every new button combination here.

namespace gibgob
{

static const char* const kHelpMain[] = {
    "D-pad: move.  Left from step 1: track name",
    "Middle: open the track's sequencer",
    "Middle on name: track settings",
    "Sliced sample = Slice track",
    "Insert: add note   Delete: delete step",
    "Insert on name: mute / unmute",
    "Top row: play tracks (record with Rec)",
    "Hold Note/Instr/FX1/FX2 + +/-: Len/Vel/Nud/Pitch",
    "Small +/-: step length   Big +/-: zoom",
    "On name: small +/- sound",
    "Hold 1 + small +/-: next / previous pattern",
    "Hold 1 + big +: Gibbon vision",
    "Rec: live rec (stopped: 1 bar count-in)",
    "Hold Rec 1 s: quantized rec on / off",
    "Play: play / stop (stop ends live rec)",
    "Button 2: this track's sound",
    "Shift+Rec: undo",
    "Shift+Play: play song",
    "Shift+Insert: duplicate track",
    "Shift+Delete: clear track",
    "Shift+Left/Right: 4 steps",
    "Shift, hold Note + +/-: length of all tracks",
    "Shift, hold FX1 / FX2 + +/-: BPM / swing",
    "Hold Shift + +/-: master volume",
    "Shift+top: CHIMP OUT FX (+/- Up/Down twist)",
    "Button 1: back here from any screen",
};

static const char* const kHelpPianoRoll[] = {
    "D-pad: move the ghost note",
    "Middle: place note / grab note / let go",
    "Insert: box select (again: lock, again: off)",
    "Delete: delete note(s) under the cursor",
    "Delete on empty: ghost back to one note",
    "Top 1: single note   Top 2-8: chords",
    "Slice track: top = slices, hold top+Middle=assign",
    "Hold Note/Instr/FX1/FX2 + +/-: Prob/Vel/Nud/Inv",
    "Small +/-: note length   Big +/-: zoom",
    "Rec: live rec (stopped: 1 bar count-in)",
    "Button 1: main sequencer",
    "Button 2: this track's sound parameters",
    "Shift+Rec: undo",
    "Shift+Delete: clear track",
    "Shift+Note: quantize / humanize",
    "Shift+Instr: acid generator",
    "Shift+FX1: arp + Go Ape",
    "Shift+Middle: auto audition",
    "Shift+Up/Down: octave   Shift+L/R: 4 steps",
    "Copy: copy selected notes / note here",
    "Shift+Copy: paste at the cursor",
    "Shift+top: Oct-/Oct+/Ghosts/Semi+/",
    "   Select all/Reverse/Double/Legato",
    "Ghosts: other tracks' notes (view only)",
    "Hold Rec 1 s: quantized rec on / off",
    "Hold Shift + +/-: master volume",
};

static const char* const kHelpDrum[] = {
    "Top row: steps of this row on / off",
    "Up/Down: row of 8 steps",
    "Left/Right: step (for its values)",
    "Middle: step on / off",
    "Delete: delete the step",
    "Hold Note/Instr/FX1/FX2 + +/-: Prob/Vel/Nud/Pitch",
    "Small +/-: step length   Big +/-: zoom (rows)",
    "Rec: live rec (stopped: 1 bar count-in)",
    "Nudge on step 1 can go early",
    "Button 1: main sequencer",
    "Button 2: this track's sound parameters",
    "Shift+Rec: undo",
    "Shift+Delete: clear track",
    "Shift+Play: play song",
    "Hold Shift + +/-: master volume",
};

static const char* const kHelpTrackPage[] = {
    "Up/Down: setting",
    "Middle on Name: rename (keyboard)",
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

static const char* const kHelpPerform[] = {
    "Top row: hold to play that button's effect",
    "D-pad: select a button   Delete: button off",
    "Hold Note + +/-: effect",
    "Hold Instr/FX1/FX2 + +/-: its settings",
    "Main seq: Shift + top row holds them",
    "Song mode, playing: top row (Shift: song)",
    "Hold Up or + / Down or - too: a twist",
    "  (lasts while held; let go = back to normal)",
    " Roll, Gate: 2x faster / 2x slower",
    " Stutter, Scratch: 2x faster / 2x slower",
    " Sweep, Talkbox: 2x faster / 2x slower",
    " Reverse, Comb, Bass kill: octave up / down",
    " Delay: only echoes / half the time",
    " Reverb: only reverb / freeze",
    " Vinyl: spin back up / brake harder",
    " Cut: noise on-off / next noise",
    " Halftime: faster / slower",
    " Riser: louder / quieter",
    " Track cut: mute drums / mute melodic",
    "Button 1: main sequencer   Play: play/stop",
    "Saved on the SD card for every project",
};

static const char* const kHelpVision[] = {
    "Top row: hold CHIMP OUT FX (no Shift)",
    "  + Up/+ or Down/-: twist while held",
    "Shift+top row: play the tracks",
    "D-pad: pick a track",
    "Insert: mute / unmute   Shift+Insert: solo",
    "Middle: open the track's sequencer",
    "Shift+Delete: clear the track",
    "Hold 1 + small +/-: next / previous pattern",
    "Hold 1 + big -, or tap 1: main sequencer",
    "Play: play / stop   Shift+Play: song",
    "Button 2: the track's sound parameters",
    "Picture: /GibGob/Vision/1.jpg and 2.jpg",
    "  up to 254 x 109 pixels, baseline JPEG",
    "  (the two frames swap on every beat)",
    "Logo: /GibGob/Vision/logo.jpg (264 x 56)",
    "Solo ends when you leave this view",
};

#define GIBGOB_HELP(arr) (arr), (int)(sizeof(arr) / sizeof((arr)[0]))

} // namespace gibgob

#endif

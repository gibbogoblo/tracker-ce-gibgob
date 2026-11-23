# Drum overview: agreed design (to build after the merged main screen)

Look: the clean beach version, `v5_*.png` (source `mockups_v5.py`).

## Screen
- Sunset sky strip at the top. Sun in the top left with bongos on it; the page
  of the track is in the sun ("1/8"). No page name.
- Clouds (flat bottom, fluffy top, each a different shape):
  4 for the colour-button values of the step under the cursor, 1 (right) for
  the selected track's length.
- One band per drum track, coloured from sunset sky -> water -> foam -> sand.
  Fewer tracks spread over those colours. The bands always fill the screen
  (2 tracks = 2 big bands). 3-5 tracks is the sweet spot; 8 must still work.
- The selected track is a much wider band: its name very bold, the parameter
  names small on the left (2 above the name, 2 below), and each step's locked
  values above / below that step (only where a step has a lock).
- 8 steps on screen (one per top button). Every hit is the same size (bigger
  on the selected track); empty steps are small soft dots.
- Cursor: yellow ring. Playhead: faint light column.
- Bottom: 8 coconuts = the 8 buttons; a solid red-orange ring where the
  selected track has a hit.
- Text white on dark bands, dark on light bands.

## Controls
- Top row: steps 1-8 of the selected track on / off (Shift: CHIMP OUT).
- Hold a colour button + +/-: edit that value and lock it on the step.
  - Note / Instr / FX1 / FX2: prob (with conditions 1:2, 2:4, FILL...), vel,
    nudge, pitch.
  - With Shift on: ratchet, sample, FX1, FX2 (labels and clouds switch too).
- Ratchet: small +/- = speed, big +/- = length.
- Sample: switch the sample on that step.
- FX1 / FX2 held: the effect list drops down from its cloud. D-pad Up/Down
  picks the effect; small +/- and big +/- change its 2 values.
  Effects: off, delay, reverb, chorus, crush, reverse, comb, filter, drive.
- Nothing held, big +/-: the selected track's length 8 / 16 / 32 / 64 / 128.
- Copy: copy. Shift + Copy: paste.
- Insert: to be decided (not "select").

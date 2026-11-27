# LXR-02 kits

The kits PERC's presets come from: Erica Synths LXR-02 kits, as distributed with Elixir
(github.com/dmajka/elixir, `assets/kits`) with Erica Synths' permission. They are the work of
Erica Synths and Hrtl, not GibGob OS's own code. GibGob OS is for personal use.

- `factory/` — the 85 distinct kits of the LXR-02 factory SD image (06.07).
- `hrtl-dlc/` — Hrtl's LXR-02 DLC kits (16).
- `hrtl-untz/` — Hrtl's LXR-02 UNTZ kits (16).

Each file is an LXR-02 `.SND` kit (8 name bytes, then the LXR's 247 sound parameters in the order of
the LXR-02 firmware's `Core/Inc/Parameters.h`). `../tools/gen_perc_kits.py LXR_DESKTOP_APP_DIR` turns them into
`../PercKits.h` (run it again after adding kits here).

The originals, and the instrument:

- Erica Synths LXR-02: https://www.ericasynths.lv/drum-synthesizer-lxr-02-1132/
- Factory content: https://www.ericasynths.lv/service/file/download/product_id/754/file_id/418/
- Hrtl DLC: https://www.ericasynths.lv/service/file/download/product_id/754/file_id/462/
- Hrtl UNTZ: https://www.ericasynths.lv/service/file/download/product_id/754/file_id/625/

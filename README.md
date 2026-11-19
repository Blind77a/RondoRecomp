# RondoRecomp

[![release](https://img.shields.io/github/v/release/gminustwo/RondoRecomp?filter=v%2A&sort=semver&display_name=tag&label=release&color=orange)](https://github.com/gminustwo/RondoRecomp/releases/latest)
[![downloads](https://img.shields.io/github/downloads/gminustwo/RondoRecomp/total?label=downloads&color=brightgreen)](https://github.com/gminustwo/RondoRecomp/releases)

Static recompilation of *Castlevania: Rondo of Blood* (PC Engine Super CD-ROM²) into a native PC application.

## Changes in this fork

This fork of [gminustwo/RondoRecomp](https://github.com/gminustwo/RondoRecomp) adds these
options to the in-game menu's Video page (F1 / Back):

- **Widescreen 16:9** (off by default): shows more of the level on each side, 42 game pixels
  a side with the TV's 4:3 pixels and 70 with square pixels. Background, enemies, items and
  Stage 2's rain fill the wider picture, and the score display moves to the left edge. Rooms
  the camera doesn't scroll through sideways (shafts, boss rooms) stay 4:3. Off in
  RetroAchievements hardcore mode.
- **Level ends** (on by default): at the start or end of a level, the extra width goes to the
  side the level continues on, so there's no black past the level's wall. The picture stays
  still until Richter has walked far enough for the camera to scroll, and enemies on that side
  appear further out so they don't pop into view.
- **Wide menus** (on by default): the file select menus go widescreen too, with their wavy
  background carried on seamlessly to the sides. Screens whose art doesn't repeat (the title
  screen, story scenes) stay 4:3.
- **Title art** (on by default, with widescreen): your own art beside still screens such as
  the title and the stage map, from files such as `title_sides.png` and `map_sides.png` in
  your profile folder. The files hold only the sides, as the game's own colour numbers, plus
  the screen's tile layout to recognise it by: none of the game's picture. Your sides appear
  with the screen from its first frame and fade and flash with it, lightning included.
- **Smooth fades** (off by default): the game's fades are blended in 24-bit colour instead of
  stepping through its 8 levels a channel.
- **Smooth dithering** (off by default): the game's regular checkerboard dithering, such as
  the burning town's sky, is drawn as the colour it stands for. Hand-drawn texture is left as
  it is.

Widescreen also keeps the HUD and a boss's health bar at the screen's edges, brings enemies
in from beyond the wider edges, and carries on scenery the game drew only as wide as the
4:3 screen (Stage 2's bridge).

Smooth fades and Smooth dithering change only the picture: the game plays the same, and save
states are unaffected. See [docs/PLAYER_README.txt](docs/PLAYER_README.txt) for details.

## Supported Platforms

Currently Windows, Linux x86 (Steam Deck).

> If you want support for a different platform and want to contribute, please feel free to put in a pull request to add that support in.

## Issues
If you have any bugs to report or issues, please use the issues tab to request what's happening. Include as much info as you can. "Game won't work for me" with no info will be closed.

## Game Versions Supported
RondoRecomp works with four releases of *Rondo of Blood*:

PC Engine disc images (a .cue file plus its track files: .bin, .img, .iso and/or .wav; zipped, 7z'd or in a folder), or a .chd made with chdman
- The original Japanese release, Akumajou Dracula X: Chi no Rondo, for the PC Engine Super CD-ROM².
- The English fan translation of that disc. Both the 2012 version and the 2024 redump-style version work.

Virtual Console releases (you supply the console key, which the game doesn't include)
- The Wii .wad, either the USA or the European release. It's the untranslated Japanese game, so it installs as the Japanese disc. If you've already installed it in Dolphin, no key is needed.
- The Wii U title (WUP). This is Konami's English translation, so it installs as the English disc.

If you install one Japanese source and one English source, the Language option in the game switches between them. RetroAchievements counts both as the same game.

All the discs share the same track layout and boot sector. The CD audio is found by its exact position on the disc, so a disc image with a different track layout won't work. **Other versions, such as the PSP's Dracula X Chronicles or the PS4's Castlevania Requiem, aren't supported.**

> [!IMPORTANT]
> **No game data is included or committed.** Tools read your own disc image (CUE sheet with
ISO/WAV/BIN tracks); everything derived from it goes to `work/`, which is git-ignored.

## Features
- Rewinding up to 30 min that works like the Konami Castlevania collections
- Compatible with the original PC Engine version, English patched, Wii, and Wii U versions
- Full suite of video options including filters and screen effects
- 16:9 widescreen, with level ends and menus filled in rather than black, and your own art beside the title and stage map
- Smooth fades and smooth dithering
- Save stating
- Cheats
- Super Castlevania IV moves (Gameplay page): 8-way whipping and dove throwing, and a limp whip, after Pizzano's Super Rondo of Blood hack (RondoRecomp's own version)
- RetroAchievements
- Portable mode: tick Portable in Setup (or make a `user` folder next to `rondo.exe` or the AppImage) and options, saves and states are kept there
- 4:3 screens (arcade cabinets): Setup's window always fits the screen and has a 4:3 layout (picked on a 4:3 screen, or with its "4:3 screen" checkbox or `--4:3`); the game's start screen and menus are laid out for 4:3 too

## Analysis tools

Requires Python 3.10+.

```sh
python tools/survey_disc.py   <game.cue>   # track layout, IPL header, data-track comparison
python tools/explore_boot.py  <game.cue>   # boot program: BIOS calls, loader → work/boot.asm
python tools/list_overlays.py <game.cue>   # every overlay group the main loader can load
python tools/explore_main.py  <game.cue>   # resident code map (bank-aware) → work/main.asm
```

Findings so far: [docs/findings.md](docs/findings.md).

## Status

- **Static recompiler:** Setup recompiles the game's code from the player's disc into C and
  builds a native `rondo.exe` with a bundled Zig compiler. On a 20,000-frame run through
  Stage 0, 99.3% of CPU cycles run natively; 402,593 native chunk executions were checked
  against the interpreter with no differences. See [docs/recompiler.md](docs/recompiler.md).
- **Runtime:** HuC6280 interpreter (253/256 opcodes pass every SingleStepTests case), PC Engine
  video, sound (PSG effects, ADPCM voices, CD audio), backup RAM saves, save states, rewind,
  and the System Card functions the game uses, reimplemented in C (no BIOS needed).
- A gothic start screen (a candlelit town at night under a pale moon) and menus in arched
  gold-and-umber frames with blackletter titles, with controller, keyboard and mouse.
- An in-game menu (F1 / Back): video modes for 4K, 16:9 and ultrawide screens (4:3 with
  blurred sides, square pixels, stretch, integer scaling, sharp filtering), a 16:9 widescreen
  option (off by default) that shows more of the level on each side (with the picture moving
  over at level ends and the file select menus wide too), smooth fades and smooth dithering,
  cinematic
  skipping, cheats (infinite health, hearts, lives), modern controller layout with
  sub-weapon / Item Crash / backflip buttons, rebindable controls (keys of your own to move, such
  as WASD, alongside the arrows), and a Japanese option (with your Japanese disc).
- The original Japanese disc and the English translation, installed side by side (the
  Japanese story text uses a bundled public-domain kanji font in place of the System Card's).
- Richter's run from Symphony of the Night (hold LB; on by default, Gameplay page turns it off), and Maria cheats (unlocked; play as her
  from the Prologue on). See [docs/runtime.md](docs/runtime.md).
- [RetroAchievements](https://retroachievements.org/game/4855): achievements, leaderboards and
  hardcore mode through RetroAchievements' own client library (rcheevos), for both discs.
  RetroAchievements hasn't recognised Rondo Recomp for hardcore yet. Until it does, its server
  records unlocks as softcore, doesn't keep leaderboard entries, and shows a "Warning: Unknown
  Emulator" notice when the game starts. The game then says "softcore only".
- Windows and Linux (x86-64, glibc 2.35 or newer: the Steam Deck, Ubuntu 22.04, Debian 12,
  Fedora 36, Arch and later).
- Setup takes the disc image zipped, as a 7z, unzipped or as a `.chd` (MAME's compressed CD
  format, which the game reads as it is: about half the space; see
  [docs/distribution.md](docs/distribution.md#chd)), or the Virtual Console releases:
  the Wii's `.wad` (the Japanese game) and the Wii U's title (WUP, the English translation),
  with the console key the player supplies, or a `.wad` installed in Dolphin. See
  [docs/distribution.md](docs/distribution.md#virtual-console).
- Updates from the game itself: the start screen says when a new release is out, and its
  Updates page downloads it (a few MB on Windows, the new AppImage on Linux), checks it,
  installs it, rebuilds the game and restarts it. See [docs/distribution.md](docs/distribution.md#updates).

## For players

Download the release (`RondoRecomp-<version>-windows-x64.zip`, or
`RondoRecomp-<version>-x86_64.AppImage` for Linux and the Steam Deck), run
`RondoRecomp Setup` (on Linux, the AppImage), choose your own disc image (zipped, or the
folder it is unzipped in) or Virtual Console release, then click Play. On the Steam Deck, add the AppImage to Steam as a
non-Steam game. See [docs/PLAYER_README.txt](docs/PLAYER_README.txt) and
[docs/distribution.md](docs/distribution.md).

## Layout

- `runtime/cpu/` HuC6280 interpreter and shared instruction semantics
- `runtime/pce/` PC Engine system, video, disc (cue sheets, and .chd images read in place), backup RAM, System Card replacement
- `runtime/recomp/` native-code dispatch and verification
- `tests/` conformance test runners
- `tools/pce/` platform code: disc access, HuC6280 decoder, System Card table, banked code explorer
- `tools/rondo/` game-specific formats (overlay loader)
- `tools/setup/` Setup (command line and window): zip, 7z and .chd import, disc check, recompile, build with zig cc
- `tools/recomp/` code generator and capture script
- `tools/frontend/` windowed frontend (SDL3)
- `docs/` notes
- `work/` local output and test data (ignored)

# CVX60: true 60fps for Resident Evil CODE: Veronica X (GameCube, PAL and USA) in Dolphin

An Action Replay code that makes RE CODE: Veronica X render at 60fps in Dolphin **without speeding
anything up**. The game still runs its logic at its native 30 ticks per second, so timers, enemy AI,
animations, physics, audio and input are exactly as in the original. Each tick is shown as two frames:

1. a frame halfway between the previous tick and this one (camera, characters and enemies interpolated),
2. the exact frame of this tick.

The result is smooth 60fps motion for Chris/Claire, enemies, the camera (follow cameras and swings) and the
room, with about one extra frame (16 ms) of display latency.

## Requirements

- Resident Evil CODE: Veronica X, your own disc image (Disc 1 and Disc 2):
  - **PAL (GCDP08)**: use `CVX60_GCDP08.ini`;
  - **USA (GCDE08)**: use `CVX60_GCDE08.ini`.
- Dolphin 5.0-21460 or newer (tested on 5.0-21460, JIT64, D3D11).
- For PAL Users: the game must run in **60Hz** mode: enable a PAL 60Hz code (e.g. "Enable 60hz Output for PAL
  version", hooks `0x80178560`). CVX60 does nothing in 50Hz mode. The USA version is 60Hz already.
- Cheats enabled (Config > General > Enable Cheats).

## Install

- **Option A) Through Dolphin (easiest)**
     1. Right-click the game in Dolphin > Properties > AR Codes > Add New Code.
     2. PAL only: name it "Enable 60hz Output for PAL version" and paste the 7 code lines
        under that heading in CVX60_GCDP08.ini. Save.
     3. Add New Code again, name it "60 FPS (interpolated) [CVX60]" and paste all the lines
        under that heading from the INI for your version (2,000-3,000 lines). Save.
     4. Tick the code(s) and start the game.

- **Option B) Edit the game's INI file**
     1. Open your Dolphin user folder (Windows: Documents\Dolphin Emulator),
        then GameSettings\GCDP08.ini (PAL) or GameSettings\GCDE08.ini (USA); create it if needed.
     2. If the file is new or empty, copy the matching CVX60_GCDP08.ini / CVX60_GCDE08.ini into it.
        If it already has an [ActionReplay] section, paste the two codes at the end of that
        section and add their two "$" names to [ActionReplay_Enabled]. Don't create a
        second [ActionReplay] section.
     3. Start the game.

  Changes to cheats take effect when the game boots, so restart the game after adding them.

  
## What it does (technical)

- Hooks the main loop's `njWaitVSync` call, the logic/draw seam in `bhMainSequence`, the three draw-all
  calls and `bhPutModel`. Code and state live in the free OS arena above the game's heaps
  (`0x817B7000`..), and ArenaLo is raised past it.
- Frame A: per-bone world matrices (`O_WORK`) and the view matrix are blended halfway between the
  previous and current tick (camera by eye position + orientation, bones renormalised). Lights are
  re-applied for the blended camera.
- Frame B is copied out at the same point of its field as frame A, so both stay on screen equally long.
- Frame B: a draw-only pass with the draw-time camera, bones, FOV and room lights, then its own XFB copy,
  `VIFlush` and present, without the end-of-render callback or sound sync, so no game state advances.
- Door transitions: after each door tick the camera path and door pose are snapshotted; the scene is redrawn
  by the game's own bhControlDoor with its logic paused (status bit 0x80), once at the midpoint and once exact.
- Message/subtitle windows stay at 60: the exact frame redraws the window with the message state saved
  and restored (no buttons held, so no sounds can repeat).
- Scripted scenes (event camera / forced fixed cut): models and effects are drawn exact on a cut and the
  tick after (scripts re-pose characters there), and a whole-skeleton check keeps re-posed characters exact.
- Falls back to vanilla 30fps for anything unusual: menus, inventory/map, mirrors, scopes,
  FMVs, pause, countdown HUD. Frame B is dropped when a cut relocates the camera after the
  draw, so cuts are clean.

## Known limits

- Shadows, 3D effects (sparks, smoke, flashes) and bone-attached effects (muzzle flash, laser sight) are
  interpolated. Pre-built polygon effects (ef_pol), function-driven effects (ef_fnc) and 2D/HUD elements still
  update at 30.
- Mirror rooms, scope and thermal views, and message boxes run at vanilla 30fps.
- Addresses are for GCDP08 rev 0 and GCDE08 rev 0 (regions.h). USA addresses were translated from PAL by
  code-pattern alignment (tools/xlate.py) and match the GameCube decompilation's symbols.
- USA has much less free memory: up to 200 bones per tick are interpolated (the player always), beyond that
  extra models are drawn exact.
- Savestates contain the mod's code and state. A savestate made with one CVX60 version can freeze when loaded
  with another (the mod resets its own state on a version change, but can't recover a state saved while the CPU
  was inside the old code). Same-version savestates and memory card saves are unaffected.

## Building

Requires Python 3.10+ with `pip install ziglang pyelftools` (plus `capstone` is optional). `build.py` compiles `cvx60.c` with Zig's clang (`powerpc-freestanding-eabihf`, `-mcpu=750`), links it at
`0x817B7000` and emits the AR code. `build.py --usa` builds the USA code; `build.py --lab` adds diagnostics and a test harness (pad injection,
frame traces) for `tools/cvxdbg.py`.

## Credits

Built with AI assistance (Claude) and the universal-modder toolkit. 
The bundled PAL 60Hz code was created by Ralf@gc-forever on GC Forever forums
Engine names and structure come from the community decompilations of CODE: Veronica X:
[recvx-decomp (PS2)](https://github.com/AshfordFamily/recvx-decomp) and
[recvx-gc-decomp](https://github.com/fmil95/recvx-gc-decomp). 
Ghidra + GameCube Loader for analysis
No game code or assets are included.

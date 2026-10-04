# CVX60---60fps-code-for-Resident-Evil-CODE-Veronica-X-on-Gamecube-Dolphin-
CVX60 - true 60fps for Resident Evil CODE: Veronica X (GameCube, PAL and USA) in Dolphin
================================================================================

**What it does**
  
  Renders the game at 60fps WITHOUT speeding anything up. The game still runs its logic at
  its native 30 ticks per second; every tick is shown as two frames (an in-between frame,
  then the exact one). Smooth at 60: characters, enemies, the camera, shadows, gun flashes,
  sparks/smoke, in-game cutscenes, subtitles/message boxes, alarm-light rooms and door
  transitions.
  Menus, inventory, map and FMVs stay at 30.

**Requirements**
  - Resident Evil CODE: Veronica X (Disc 1 and Disc 2), one of:
      PAL / Europe, game ID GCDP08  -> use CVX60_GCDP08.ini
      USA,          game ID GCDE08  -> use CVX60_GCDE08.ini
    (Dolphin shows the game ID in the game list.) The Japanese version is not supported.
  - Dolphin (tested on 5.0-21460). Dolphin only - this is not for a real Action Replay.
  - Config > General > "Enable Cheats" turned on.
  - PAL only: the game must run in 60Hz mode. CVX60_GCDP08.ini includes the
    "Enable 60hz Output for PAL version" code for that (made by Ralf@gc-forever
     on GC Forever forums); keep both codes enabled.
    The USA version already runs at 60Hz and needs only the CVX60 code.
  - USA only: Enabling a "Door Skip" code together with CVX60 may
     cause issues, although brief tests looked fine (they patch the same spot).

**Install (pick one)**

  A) Through Dolphin (easiest)
     1. Right-click the game in Dolphin > Properties > AR Codes > Add New Code.
     2. PAL only: name it "Enable 60hz Output for PAL version" and paste the 7 code lines
        under that heading in CVX60_GCDP08.ini. Save.
     3. Add New Code again, name it "60 FPS (interpolated) [CVX60]" and paste all the lines
        under that heading from the INI for your version (2,000-3,000 lines). Save.
     4. Tick the code(s) and start the game.

  B) Edit the game's INI file
     1. Open your Dolphin user folder (Windows: Documents\Dolphin Emulator),
        then GameSettings\GCDP08.ini (PAL) or GameSettings\GCDE08.ini (USA); create it if needed.
     2. If the file is new or empty, copy the matching CVX60_GCDP08.ini / CVX60_GCDE08.ini into it.
        If it already has an [ActionReplay] section, paste the two codes at the end of that
        section and add their two "$" names to [ActionReplay_Enabled]. Don't create a
        second [ActionReplay] section.
     3. Start the game.

  Changes to cheats take effect when the game boots, so restart the game after adding them.

**Tips**
  - Savestates include the mod itself: after updating CVX60 to a new version, make new savestates.
    A savestate made with an older version may freeze. Memory card (typewriter) saves are unaffected.
  - To compare with the original, untick the CVX60 code and restart the game.

**Known limits**
  - Some blood splats/trails, shell casings, a few special effects and all menus update at 30.
  - USA: in very crowded scenes some characters may be drawn without smoothing (less free memory
    on that version); the player always is.
  - Mirror rooms, scope/first-person views and message boxes run at the original 30fps.
  - About one extra frame (16 ms) of display latency.

**Credits**
  Built with AI assistance (Claude). Engine knowledge from the community decompilations of
  CODE: Veronica X: recvx-decomp (PS2) and recvx-gc-decomp (GameCube). Analysis with Ghidra
  and the GameCube Loader. 
  The PAL 60Hz code was created by Ralf@gc-forever on GC Forever forums
  This code contains no game data; you need your own copy of the game.

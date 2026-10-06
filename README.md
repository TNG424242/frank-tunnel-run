# FRANK TUNNEL RUN — ESP32 CYD 2432S028

Side-scrolling platformer starring Frank (beige shirt + shorts, cowboy hat,
Mario-style). Frank auto-runs through tunnels. TAP to jump (hold = higher).

- Board: ESP32 CYD 2432S028R (ILI9341 320x240 + XPT2046 touch)
- Framework: Arduino + LovyanGFX (OTA partition layout so the game can
  switch back to the SD launcher on-device)
- Controls: TAP anywhere to jump, HOLD for higher jumps (single-touch safe)
- Hazards: fire pits/strips, rolling boulders, tribesmen spears
- Stomp tribesmen (+100), grab coins (+10), reach EXIT (+500)
- 3 lives, 2.5s spawn protection, run speed rises every phase
- Exit to launcher: HOLD top-left corner 3s → `EXIT TO LAUNCHER?` → TAP to
  confirm (flashes `/launcher.bin` from SD and reboots; 6s wait cancels).
  Serial `exit` + Enter does the same. Hint shown on the title screen.

## Levels (ASCII maps in src/main.cpp, `LV1`... — ragged rows OK)
- `#` solid brick/ground, `=` platform, `C` coin, `F` fire
- `T` tribesman, `B` boulder spawner, `E` exit (2 tall), `X` treasure chest, `P` start
- Level 1: FIRST TUNNEL. Level 2: BOULDER HILL (bonus: climb stairs,
  dodge tumbling boulders, chest = 100 coins = +1000). Level 3: SUNKEN
  POOL (swim mode: DRAG up/down to steer, no jumping; snakes weave,
  barracudas charge in-lane, shark holds ahead and lunges; thermal
  vents burn; pearls +10; reach the sunlit cave mouth). Level 4: FROG
  BOG (platform-hopping over pits; poisonous frogs hop at Frank, never
  stompable; bats circle anchors, untouchable). Level 5: SGT BLACK
  (boss arena: soldier in black stalks Frank and fires tracers, faster
  when hurt; land on his head 5+ times, +200 per hit, +1000 KO).
  More appended to `LEVELS[]` as designed (`S`/`R`/`J`/`G`/`W`/`K`
  enemies, `LEVEL_MODE` 1 = swim, 2 = boss).

## Artwork (320x240 JPEG in `include/`, PROGMEM)
- Title: `Frank_Tunnel_Run.jpg` → `intro_img.h` (`INTRO_JPG`), fullscreen on TITLE with TAP banner at top
- Mid-clear (LV1-4): `Frank_Level_Cleared.jpg` → `cleared_img.h` (`CLEARED_JPG`)
- Final win (LV5 SGT BLACK KO): `Frank_Wins.jpg` → `wins_img.h` (`WINS_JPG`)
- Sources in `/home/tng/Work/*.jpg`, resized/crop to 320x240 q85 before embedding

## Build + flash (/dev/ttyUSB0)
```
pio run -e cyd-2432s028
pio run -e cyd-2432s028 -t upload --upload-port /dev/ttyUSB0
```
Or `./flash.sh`.

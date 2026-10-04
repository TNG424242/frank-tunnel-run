/*
 * FRANK TUNNEL RUN — ESP32 CYD 2432S028 (LANDSCAPE 320x240)
 * Side-scrolling platformer. Frank (beige shirt+shorts, cowboy hat,
 * Mario-style) auto-runs through tunnels. TAP to jump (hold = higher).
 * Dodge fire pits, rolling boulders, tribesmen spears. Grab coins, stomp
 * tribesmen, reach the exit door. Touch: XPT2046 (single-touch resistive).
 */
#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <cstdarg>
#include "intro_img.h"
#include "cleared_img.h"
#include "wins_img.h"

// Manual CYD 2432S028 config (from LovyanGFX Sunton preset).
// LCD: MOSI 13, MISO 12, SCLK 14, DC 2, CS 15, RST -1, BL 21 (HSPI)
// Touch XPT2046: software SPI SCLK 25, MOSI 32, MISO 39, CS 33
class LGFX_CYD : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9342 _panel;
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;
  lgfx::Touch_XPT2046 _touch;

public:
  LGFX_CYD(void) {
    {
      auto cfg = _bus.config();
      cfg.spi_host = HSPI_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 20000000;
      cfg.freq_read = 8000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = 1;
      cfg.pin_sclk = 14;
      cfg.pin_mosi = 13;
      cfg.pin_miso = 12;
      cfg.pin_dc = 2;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = 15;
      cfg.pin_rst = -1;
      cfg.pin_busy = -1;
      cfg.memory_width = 320;
      cfg.memory_height = 240;
      cfg.panel_width = 320;
      cfg.panel_height = 240;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = true;
      cfg.invert = true; // ILI9342 on CYD needs inversion for true black
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      _panel.config(cfg);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl = 21;
      cfg.invert = false;
      cfg.freq = 44100;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    {
      auto cfg = _touch.config();
      cfg.x_min = 300;
      cfg.x_max = 3900;
      cfg.y_min = 3700;
      cfg.y_max = 200;
      cfg.pin_int = -1;
      cfg.bus_shared = false;
      cfg.offset_rotation = 3; // align X increasing left-to-right
      cfg.spi_host = (spi_host_device_t)-1;
      cfg.freq = 1000000;
      cfg.pin_sclk = 25;
      cfg.pin_mosi = 32;
      cfg.pin_miso = 39;
      cfg.pin_cs = 33;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};

static LGFX_CYD lcd;
static LGFX_Sprite fb(&lcd);
static bool useSprite = false;
static lgfx::LovyanGFX* GFX = nullptr;

// ---------- Constants ----------
static const int W = 320;
static const int H = 240;
static const int HUD_H = 22;
static const int TILE = 16;
static const int SCREEN_OX = 32; // shift whole game right to centre it
static const int ROWS = 15; // 240 / 16

static const int PW = 12;  // Frank width
static const int PH = 20;  // Frank height (short & stumpy)

static const int MAX_COINS = 40;
static const int MAX_FOES = 10;  // tribesmen + swim pack
static const int MAX_SPEARS = 8;
static const int MAX_ROCKS = 4;  // boulders
static const int MAX_SPAWNERS = 6;

#define SPEAKER_PIN 26
// Non-blocking jingle player: sounds never stall input (no delay() in game).
struct Note { uint16_t freq; uint16_t ms; };
static Note tune[6];
static int nNotes = 0, noteIdx = 0;
static uint32_t noteT0 = 0;
static bool tunePlaying = false;
static void playTune(const Note* n, int count) {
  nNotes = count < 6 ? count : 6;
  for (int i = 0; i < nNotes; i++) tune[i] = n[i];
  noteIdx = 0; noteT0 = millis(); tunePlaying = (nNotes > 0);
  if (tunePlaying) ledcWriteTone(0, tune[0].freq);
}
static void updateSound() {
  if (!tunePlaying) return;
  if (millis() - noteT0 >= tune[noteIdx].ms) {
    noteIdx++;
    if (noteIdx >= nNotes) { tunePlaying = false; ledcWriteTone(0, 0); return; }
    noteT0 = millis();
    ledcWriteTone(0, tune[noteIdx].freq);
  }
}
static void sfxJump()  { const Note n[] = {{600, 30}}; playTune(n, 1); }
static void sfxCoin()  { const Note n[] = {{1320, 20}, {1760, 35}}; playTune(n, 2); }
static void sfxThrow() { const Note n[] = {{400, 20}}; playTune(n, 1); }
static void sfxStomp() { const Note n[] = {{300, 35}, {700, 45}}; playTune(n, 2); }
static void sfxDeath() { const Note n[] = {{200, 150}, {90, 180}}; playTune(n, 2); }
static void sfxWin()   { const Note n[] = {{523, 70}, {659, 70}, {784, 70}, {1047, 150}}; playTune(n, 4); }

// ---------- Level maps (ASCII, ragged rows OK — padded with ' ') ----------
// # ground/brick solid | = floating platform solid | C coin | F fire (deadly)
// T tribesman | B boulder spawner | E exit door (2 tall) | P player start
static const char* LV1[] = {
  "####################################################################################################",
  "#                                                                                                  #",
  "#                                                                                                  #",
  "#                                                                                                  #",
  "#                                                                                                  #",
  "#                                                                                                  #",
  "#                                                      CCCCCC                                      #",
  "#             CCCCCC                CCCCC              ======                    CCCCC             #",
  "#             ======   CCCC         =====      CCCCC                             =====             #",
  "#     CCC                                                      B                          CCC E    #",
  "#  P                          FF            T                     FF    T               T     E    #",
  "########################  ######################  ##########################  ######################",
  "########################  ######################  ##########################  ######################",
  "########################FF######################FF##########################FF######################",
  "########################  ######################  ##########################  ######################",
};
static const int LV1_ROWS = 15;
static const char* LV5[] = {
  "########################################################################################################################",
  "#",
  "#",
  "#",
  "#",
  "#",
  "#",
  "#                                               CC   CC                            CC",
  "#                   CCC                                               CC",
  "#         CCC                 CCC       CC                  CCC                 CC        CCC       CCC       CCC",
  "#    P                        K                   FF                                 FF",
  " #######################################################################################################################",
  " #######################################################################################################################",
  " #######################################################################################################################",
  "########################################################################################################################",
};
static const char* LV4[] = {
  "##############################################################################################################",
  "#                                                                                                        #####",
  "#                                                                                                        #####",
  "#                                                                                         W              #####",
  "#                    W                                               W                                   #####",
  "#                                                                                                        #####",
  "#                      CC                  CGC                           CC                              #####",
  "#                   C C                    ===   CC            C        ====               CCC           #####",
  "#              CC   ===   CC          CCC       ====          ===  CC                  C   ===           #####",
  "#             ====       ====  CC     ===               C C        ===         C C    ===        CCCE    #####",
  "#  P                             G                       G                      G                   E    #####",
  " ############                 #######                  ######                 #######           #############",
  " ############                 #######                  ######                 #######           #############",
  " ############                 #######                  ######                 #######           #############",
  "#############                 #######                  ######                 #######           ##############",
};
static const char* LV3[] = {
  "##############################################################################################################",
  "#                        ##                            ##                            ##                  #####",
  "#                        ##                            ##                            ##                  #####",
  "#                        ##                            ##                            ##                  #####",
  "#                        ##                     S      ##                     S      ##       CCC        #####",
  "#                        ##            CCCC  R         ##            CCCC            ## R    J           #####",
  "#                        ##                       CCC  ##                            ##                  #####",
  "#                                       ##                          R ##                       ##        #####",
  "#  P  CCC               CCCC  S         ##            CCCC    S       ##            CCCC       ##  CCC   #####",
  "#                                       ##                            ##                       ##     E  #####",
  "#                                  F    ##                            ##F                      ##     E  #####",
  "##############################################################################################################",
  "##############################################################################################################",
  "##############################################################################################################",
  "##############################################################################################################",
};
static const char* LV2[] = {
  "##############################################################################################################",
  "#                                                                                                        #####",
  "#                                                                                                        #####",
  "#                                        CC    CCCB      CCC     B CCC       CCCB      CCC     CCCCC     #####",
  "#                                    CC                                                   T         X    #####",
  "#                                CC     ######################################################################",
  "#                            CC     ##########################################################################",
  "#                        CC     ##############################################################################",
  "#                    CC     ##################################################################################",
  "#     CCC               ######################################################################################",
  "#  P        FF  T   ##########################################################################################",
  "##############################################################################################################",
  "##############################################################################################################",
  "##############################################################################################################",
  "##############################################################################################################",
};

static const char** LEVELS[] = { LV1, LV2, LV3, LV4, LV5 };
static const int LEVEL_ROWS[] = { LV1_ROWS, 15, 15, 15, 15 };
static const int NUM_LEVELS = 5;
static const char* LEVEL_NAMES[] = { "FIRST TUNNEL", "BOULDER HILL", "SUNKEN POOL", "FROG BOG", "SGT BLACK" };
// 0 = run&jump, 1 = swim (drag steers depth), 2 = boss arena
static const uint8_t LEVEL_MODE[] = { 0, 0, 1, 0, 2 };
static bool swimMode = false;
static bool bossMode = false;

// ---------- Game state ----------
enum State { TITLE, PLAYING, LEVEL_CLEAR, GAME_OVER };

struct Coin { int tx, ty; bool taken; };
struct Foe { float x, y, baseX, baseY, vx, vy; uint8_t type; bool alive; uint32_t t0; };
// type: 0 tribesman | 1 snake | 2 barracuda | 3 shark | 4 frog | 5 bat
struct Spear { float x, y, vx; bool active; };
struct Rock { float x, y, vx, vy; bool active; };
struct Spawner { int tx, ty; uint32_t nextT0; };
// Sergeant Black: walks Frank down, fires tracers; stomp his head!
struct Boss { float x, y, vy; int dir; int hp, maxHP; bool alive; uint32_t shootT0, hurtT0, jumpT0; uint8_t bstate; int burstLeft; uint32_t burstT0; };
// bstate: 0 = flee, 1 = stand & volley, 2 = dash away
static Boss boss = {0, 0, 0, -1, 5, 5, false, 0, 0, 0, 0, 0, 0};

static State state = TITLE;
static const char** tmap = LV1;
static int mapRows = LV1_ROWS;
static int mapW = 0; // tiles wide (max row length)

static float px = 0, py = 0; // player top-left
static float vx = 0, vy = 0;
static bool grounded = false;
static bool jumpHeld = false;
static uint32_t jumpBufT0 = 0; // air-tap timestamp: buffered jump fires on landing
static float runSpeed = 1.6f;
static float camX = 0;
static float exitX = 0;
static float exitY = 0;
static bool exitIsChest = false; // bonus levels end at a treasure chest, not a door

static Coin coins[MAX_COINS];
static int nCoins = 0;
static Foe foes[MAX_FOES];
static int nFoes = 0;
static Spear spears[MAX_SPEARS];
static Rock rocks[MAX_ROCKS];
static Spawner spawners[MAX_SPAWNERS];
static int nSpawners = 0;

static int score = 0, hiScore = 0, lives = 3, level = 1;
static uint32_t lastFrame = 0, stateChangeMs = 0, protectT0 = 0;
static bool touching = false, wasTouching = false;
static uint32_t touchStartMs = 0;
static int touchSX = 0, touchSY = 0; // last live touch pos (screen px)
static int prevTX = 0; // previous frame touch x (boss slide-drag)

// ---------- Helpers ----------
static uint16_t col(uint8_t r, uint8_t g, uint8_t b) { return lcd.color565(r, g, b); }

// Clipped primitives: NEVER issue off-screen windows.
// SCREEN_OX shifts every pixel right to centre the game on this panel.
static inline void R(int x, int y, int w, int h, uint16_t c) {
  x += SCREEN_OX;
  if (w <= 0 || h <= 0) return;
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x >= W || y >= H) return;
  if (x + w > W) w = W - x;
  if (y + h > H) h = H - y;
  if (w <= 0 || h <= 0) return;
  GFX->fillRect(x, y, w, h, c);
}
static inline void P(int x, int y, uint16_t c) {
  x += SCREEN_OX;
  if (x < 0 || y < 0 || x >= W || y >= H) return;
  GFX->drawPixel(x, y, c);
}
static inline void C(int x, int y, int r, uint16_t c) {
  x += SCREEN_OX;
  if (x + r < 0 || y + r < 0 || x - r >= W || y - r >= H) return;
  GFX->fillCircle(x, y, r, c);
}
static inline void OC(int x, int y, int r, uint16_t c) {
  x += SCREEN_OX;
  if (r <= 0 || x + r < 0 || y + r < 0 || x - r >= W || y - r >= H) return;
  GFX->drawCircle(x, y, r, c);
}
static inline void L(int x0, int y0, int x1, int y1, uint16_t c) {
  x0 += SCREEN_OX; x1 += SCREEN_OX;
  if (x0 < 0 || y0 < 0 || x0 >= W || y0 >= H) return;
  if (x1 < 0 || y1 < 0 || x1 >= W || y1 >= H) return;
  GFX->drawLine(x0, y0, x1, y1, c);
}

static char tileAt(int tx, int ty) {
  if (tx < 0 || ty < 0 || ty >= mapRows || tx >= mapW) return ' ';
  int len = strlen(tmap[ty]);
  if (tx >= len) return ' ';
  return tmap[ty][tx];
}
static bool solidAt(int tx, int ty) {
  char t = tileAt(tx, ty);
  return t == '#' || t == '=';
}
static bool solidPx(float x, float y) {
  return solidAt((int)(x / TILE), (int)(y / TILE));
}
// AABB vs solid tiles
static bool collide(float x, float y, float w, float h) {
  int x0 = (int)(x / TILE), x1 = (int)((x + w - 0.01f) / TILE);
  int y0 = (int)(y / TILE), y1 = (int)((y + h - 0.01f) / TILE);
  for (int ty = y0; ty <= y1; ty++)
    for (int tx = x0; tx <= x1; tx++)
      if (solidAt(tx, ty)) return true;
  return false;
}
static bool hazardPx(float x, float y, float w, float h) {
  int x0 = (int)(x / TILE), x1 = (int)((x + w - 0.01f) / TILE);
  int y0 = (int)(y / TILE), y1 = (int)((y + h - 0.01f) / TILE);
  for (int ty = y0; ty <= y1; ty++)
    for (int tx = x0; tx <= x1; tx++)
      if (tileAt(tx, ty) == 'F') return true;
  return false;
}

// true if damage counts (outside spawn protection)
static bool damageOK() {
  uint32_t now = millis();
  if (now - protectT0 < 2500) return false;
  return true;
}

static void startLevelKeepLives();
static void killPlayer(uint32_t now) {
  if (!damageOK()) return;
  lives--;
  sfxDeath();
  if (lives <= 0) { state = GAME_OVER; stateChangeMs = now; return; }
  // respawn at level start
  startLevelKeepLives();
}

static void startLevel() {
  int idx = (level - 1) % NUM_LEVELS;
  tmap = LEVELS[idx];
  mapRows = LEVEL_ROWS[idx];
  mapW = 0;
  for (int r = 0; r < mapRows; r++) mapW = max(mapW, (int)strlen(tmap[r]));

  nCoins = nFoes = nSpawners = 0;
  exitX = (mapW - 4) * TILE;
  exitY = (mapRows - 2) * TILE;
  exitIsChest = false;
  swimMode = (LEVEL_MODE[idx] == 1);
  bossMode = (LEVEL_MODE[idx] == 2);
  boss.alive = false;
  for (auto &s : spears) s.active = false;
  for (auto &r : rocks) r.active = false;

  uint32_t now = millis();
  for (int ty = 0; ty < mapRows; ty++) {
    int len = strlen(tmap[ty]);
    for (int tx = 0; tx < len; tx++) {
      char t = tmap[ty][tx];
      if (t == 'P') { px = tx * TILE + 2; py = ty * TILE + TILE - PH; }
      else if (t == 'C' && nCoins < MAX_COINS) { coins[nCoins++] = {(int16_t)tx, (int16_t)ty, false}; }
      else if (t == 'T' && nFoes < MAX_FOES) {
        foes[nFoes].x = tx * TILE + 2; foes[nFoes].y = ty * TILE + TILE - 14;
        foes[nFoes].baseX = foes[nFoes].x; foes[nFoes].baseY = foes[nFoes].y;
        foes[nFoes].vx = 0; foes[nFoes].vy = 0; foes[nFoes].type = 0;
        foes[nFoes].alive = true; foes[nFoes].t0 = now + 1500 + random(0, 1500);
        nFoes++;
      }
      else if (t == 'G' || t == 'W') {
        if (nFoes < MAX_FOES) {
          foes[nFoes].x = tx * TILE + 2; foes[nFoes].y = ty * TILE + TILE - 10;
          foes[nFoes].baseX = foes[nFoes].x; foes[nFoes].baseY = foes[nFoes].y;
          foes[nFoes].vx = 0; foes[nFoes].vy = 0;
          foes[nFoes].type = (t == 'G') ? 4 : 5;
          foes[nFoes].alive = true; foes[nFoes].t0 = now + 800 + random(0, 1200);
          nFoes++;
        }
      }
      else if (t == 'S' || t == 'R' || t == 'J') {
        if (nFoes < MAX_FOES) {
          foes[nFoes].x = tx * TILE; foes[nFoes].y = ty * TILE + 2;
          foes[nFoes].baseX = foes[nFoes].x; foes[nFoes].baseY = foes[nFoes].y;
          foes[nFoes].vx = 0; foes[nFoes].vy = 0;
          foes[nFoes].type = (t == 'S') ? 1 : (t == 'R' ? 2 : 3);
          foes[nFoes].alive = true; foes[nFoes].t0 = now + 1000 + random(0, 1500);
          nFoes++;
        }
      }
      else if (t == 'B' && nSpawners < MAX_SPAWNERS) {
        spawners[nSpawners++] = {tx, ty, now + 2000};
      }
      else if (t == 'E') { exitX = tx * TILE; exitY = ty * TILE; exitIsChest = false; }
      else if (t == 'X') { exitX = tx * TILE; exitY = ty * TILE; exitIsChest = true; }
      else if (t == 'K') {
        boss.x = tx * TILE - 1; boss.y = ty * TILE + TILE - 26; boss.vy = 0;
        boss.dir = 1; boss.maxHP = 5 + max(0, (level - 5) / 4); boss.hp = boss.maxHP;
        boss.alive = true; boss.shootT0 = now + 2500; boss.hurtT0 = 0; boss.jumpT0 = 0;
        boss.bstate = 0; boss.burstLeft = 0; boss.burstT0 = 0;
      }
    }
  }
  // coins respawn each attempt, foes revive
  for (int i = 0; i < nCoins; i++) coins[i].taken = false;
  vx = 0; vy = 0; grounded = false;
  jumpBufT0 = 0;
  if (swimMode) {
    runSpeed = 1.3f + (level - 1) * 0.1f; // slow floaty cruise
    if (runSpeed > 2.4f) runSpeed = 2.4f;
  } else if (bossMode) {
    runSpeed = 1.6f + (level - 1) * 0.05f; // deliberate duel pace
    if (runSpeed > 2.2f) runSpeed = 2.2f;
  } else {
    runSpeed = 2.0f + (level - 1) * 0.15f; // faster every phase
    if (runSpeed > 3.2f) runSpeed = 3.2f;
  }
  camX = max(0.0f, px - 80);
  protectT0 = now;
  state = PLAYING;
  Serial.printf("FRANK START lv=%d map=%d run=%.2f\n", level, idx, runSpeed);
}

// respawn after death: rebuild positions, keep score/lives/level
static void startLevelKeepLives() {
  int keepScore = score, keepHi = hiScore, keepLives = lives, keepLv = level;
  int keepBossHP = boss.hp, keepBossMax = boss.maxHP; // boss fight progress survives death
  startLevel();
  score = keepScore; hiScore = keepHi; lives = keepLives; level = keepLv;
  // startLevel already set runSpeed + swimMode for this level; nothing to redo
  if (bossMode) { boss.hp = keepBossHP; boss.maxHP = keepBossMax; }
}

// ---------- Touch ----------
// Auto-run: PRESS in PLAYING = jump. Menus advance on PRESS too (release
// coordinates are stale on XPT2046, so release-taps are unreliable).
static void updateTouch() {
  int16_t tx, ty;
  touching = lcd.getTouch(&tx, &ty);
  uint32_t now = millis();

  if (touching && !wasTouching) {
    touchStartMs = now;
    touchSX = tx; touchSY = ty;
    prevTX = tx;
    if (state == PLAYING && !swimMode && grounded) {
      vy = -8.2f; grounded = false;
      jumpHeld = true;
      jumpBufT0 = 0; // consumed immediately
      sfxJump();
    } else if (state == PLAYING && !swimMode) {
      jumpHeld = true;
      jumpBufT0 = now; // air tap: buffered, fires on landing (forgiving timing)
    } else if (state == TITLE) {
      // TEMP playtest warp: tap one of 4 screen zones = start at that level
      int warp = touchSX * NUM_LEVELS / W + 1;
      if (warp < 1) warp = 1; if (warp > NUM_LEVELS) warp = NUM_LEVELS;
      score = 0; lives = 3; level = warp; startLevel();
    } else if (state == GAME_OVER && now - stateChangeMs > 800) {
      score = 0; lives = 3; level = 1; startLevel();
    } else if (state == LEVEL_CLEAR && now - stateChangeMs > 1200) {
      level++; startLevel();
    }
  } else if (touching) {
    jumpHeld = true;
    touchSX = tx; touchSY = ty; // swim steering target follows the finger
  }
  if (!touching && wasTouching) {
    jumpHeld = false;
  }
  wasTouching = touching;
}

// ---------- Update ----------
static void updateGame() {
  uint32_t now = millis();
  if (state != PLAYING) return;

  // --- horizontal auto-run with tile collision (1-tile auto step-up on land) ---
  // (same TAP=JUMP controls in every level — single-touch can't slide+jump)
  float nx = px + runSpeed;
  if (!collide(nx, py, PW, PH)) {
    px = nx;
  } else if (!swimMode && grounded && !collide(nx, py - 16, PW, PH) && !collide(px, py - 16, PW, PH)) {
    px = nx; py -= 16; vy = 0; grounded = true; // stepped up a stair
  } else {
    // bonk into wall: stop at tile edge
    int edge = (int)((nx + PW) / TILE) * TILE - PW - 0.01f;
    px = edge;
  }

  // boss arena loops: run out the right, back in the left, chase continues
  if (bossMode && px + PW > mapW * TILE - 8) {
    px = 2 * TILE;
    for (auto &s : spears) s.active = false; // clear stray tracers on wrap
  }

  if (swimMode) {
    // --- swimming: finger steers depth, gentle sink when released ---
    float cy = py + PH / 2;
    if (touching) {
      float want = touchSY - cy;
      vy = want * 0.15f;
      if (vy < -3.4f) vy = -3.4f;
      if (vy > 3.4f) vy = 3.4f;
    } else {
      vy += 0.08f; // sink
      if (vy > 1.6f) vy = 1.6f;
    }
    float ny = py + vy;
    if (!collide(px, ny, PW, PH)) { py = ny; }
    else if (vy > 0) { py = (int)((ny + PH) / TILE) * TILE - PH - 0.01f; vy = 0; }
    else { py = ((int)(ny / TILE) + 1) * TILE + 0.01f; vy = 0; }
    if (py < HUD_H) { py = HUD_H; vy = 0; }
    if (py > ROWS * TILE - PH) { py = ROWS * TILE - PH; vy = 0; }
    grounded = false;
  } else {
  // --- vertical physics (variable jump: low gravity while holding & rising) ---
  float grav = (jumpHeld && vy < -2.0f) ? 0.45f : 0.8f;
  vy += grav;
  if (vy > 7.5f) vy = 7.5f;
  float ny = py + vy;
  grounded = false;
  if (vy >= 0) {
    if (!collide(px, ny, PW, PH)) { py = ny; }
    else {
      int edge = (int)((ny + PH) / TILE) * TILE - PH - 0.01f;
      py = edge; vy = 0; grounded = true;
      // buffered jump: tap slightly before landing still jumps (responsive!)
      if (jumpBufT0 != 0 && now - jumpBufT0 < 150) {
        jumpBufT0 = 0;
        vy = -8.2f; grounded = false;
        sfxJump();
      }
    }
  } else {
    if (!collide(px, ny, PW, PH)) { py = ny; }
    else {
      int edge = ((int)(ny / TILE) + 1) * TILE + 0.01f;
      py = edge; vy = 0;
    }
  }
  }

  float cx = px + PW / 2, feet = py + PH;

  // --- fell out of world ---
  if (py > ROWS * TILE + 10) { killPlayer(now); if (state != PLAYING) return; }

  // --- fire hazard ---
  if (hazardPx(px + 1, py + 1, PW - 2, PH - 2)) { killPlayer(now); if (state != PLAYING) return; }

  // --- camera ---
  camX = px - 90;
  if (camX < 0) camX = 0;
  float maxCam = mapW * TILE - W;
  if (maxCam < 0) maxCam = 0;
  if (camX > maxCam) camX = maxCam;

  // --- coins ---
  for (int i = 0; i < nCoins; i++) {
    if (coins[i].taken) continue;
    float ccx = coins[i].tx * TILE + 8, ccy = coins[i].ty * TILE + 8;
    float dx = cx - ccx, dy = (py + PH / 2) - ccy;
    if (dx * dx + dy * dy < 12 * 12) {
      coins[i].taken = true;
      score += 10; if (score > hiScore) hiScore = score;
      sfxCoin();
    }
  }

  // --- tribesmen: throw spears, stompable (land levels) ---
  for (int i = 0; i < nFoes; i++) {
    if (!foes[i].alive || (foes[i].type != 0 && foes[i].type != 4 && foes[i].type != 5)) continue;
    if (foes[i].type == 4) { // poisonous frog: hops at Frank, NEVER stompable
      foes[i].vy += 0.5f; if (foes[i].vy > 7) foes[i].vy = 7;
      if (foes[i].vy == 0 && now > foes[i].t0 && fabsf(cx - (foes[i].x + 6)) < 200) {
        foes[i].t0 = now + max(900, 1900 - level * 120);
        foes[i].vx = (cx > foes[i].x) ? (1.2f + level * 0.1f) : -(1.2f + level * 0.1f);
        foes[i].vy = -5.0f;
      }
      float fnx = foes[i].x + foes[i].vx;
      if (!collide(fnx, foes[i].y, 12, 10)) foes[i].x = fnx; else foes[i].vx = 0;
      float fny = foes[i].y + foes[i].vy;
      if (!collide(foes[i].x, fny, 12, 10)) foes[i].y = fny;
      else {
        if (foes[i].vy > 0) foes[i].y = (int)((fny + 10) / TILE) * TILE - 10 - 0.01f;
        else foes[i].y = ((int)(fny / TILE) + 1) * TILE + 0.01f;
        foes[i].vy = 0;
      }
      if (foes[i].y > ROWS * TILE + 20) { foes[i].alive = false; continue; } // hopped into a pit
      if (cx > foes[i].x - 2 && cx < foes[i].x + 14 && feet > foes[i].y && py < foes[i].y + 10) {
        killPlayer(now); if (state != PLAYING) return; // poison! no stomping
      }
      continue;
    }
    if (foes[i].type == 5) { // bat: circles its anchor, untouchable
      float t = (now + i * 700) / 600.0f;
      foes[i].x = foes[i].baseX + sinf(t) * 30.0f;
      foes[i].y = foes[i].baseY + sinf(t * 2.3f) * 10.0f;
      if (cx > foes[i].x - 2 && cx < foes[i].x + 16 && feet > foes[i].y && py < foes[i].y + 9) {
        killPlayer(now); if (state != PLAYING) return;
      }
      continue;
    }
    float fx = foes[i].x + 6;
    // throw when player is ahead within range
    if (now > foes[i].t0 && cx > fx - 40 && cx < fx + 190) {
      foes[i].t0 = now + max(900, 2400 - level * 150);
      for (auto &s : spears) {
        if (s.active) continue;
        s.active = true;
        s.x = fx; s.y = foes[i].y + 5;
        s.vx = (cx < fx ? -1 : 1) * (3.2f + level * 0.2f);
        break;
      }
      sfxThrow();
    }
    // touch player?
    if (cx > foes[i].x - 2 && cx < foes[i].x + 14 && feet > foes[i].y && py < foes[i].y + 14) {
      if (vy > 1.0f && feet - foes[i].y < 9) { // stomp!
        foes[i].alive = false;
        vy = -6.0f;
        score += 100; if (score > hiScore) hiScore = score;
        sfxStomp();
      } else {
        killPlayer(now); if (state != PLAYING) return;
      }
    }
  }

  // --- swim pack: snakes weave, barracudas charge, shark hunts (no stomping!) ---
  if (swimMode) {
    float pcy = py + PH / 2;
    for (int i = 0; i < nFoes; i++) {
      if (!foes[i].alive || foes[i].type == 0) continue;
      float fx = foes[i].x + 8, fy = foes[i].y + 7;
      if (fx < camX - 60) { if (foes[i].type != 3) { foes[i].alive = false; continue; } }
      if (foes[i].type == 1) { // snake: drifts left, weaves vertically
        foes[i].x -= 0.5f + level * 0.03f;
        foes[i].y = foes[i].baseY + sinf((now + i * 900) / 700.0f) * 12.0f;
      } else if (foes[i].type == 2) { // barracuda: hovers, charges when Frank is in lane
        foes[i].y = foes[i].baseY + sinf((now + i * 500) / 600.0f) * 5.0f;
        if (foes[i].vx == 0 && now > foes[i].t0 && fx > cx && fx - cx < 230 && fabsf(pcy - fy) < 48) {
          foes[i].vx = -(4.0f + level * 0.15f);
          foes[i].t0 = now + 2600; // cooldown after this charge ends
        } else if (foes[i].vx != 0 && now > foes[i].t0 - 1900) {
          foes[i].vx = 0; // charge lasts ~700ms
        }
        foes[i].x += foes[i].vx;
      } else { // shark: dormant until Frank closes in, then stalks ~110px ahead
        if (fx > cx && fx - cx < 280) {
          float step = (px + 110) - foes[i].x;
          if (step > 1.6f) step = 1.6f;
          if (step < -2.5f) step = -2.5f;
          foes[i].x += step;
          foes[i].y = foes[i].baseY + sinf(now / 1400.0f) * 38.0f;
          if (now > foes[i].t0 && fx - cx < 210 && fx > cx - 20) {
            foes[i].t0 = now + 3000;
            float dx = cx - fx, dy = pcy - fy;
            float d = sqrtf(dx * dx + dy * dy) + 0.01f;
            foes[i].x += dx / d * 34.0f;
            foes[i].y += dy / d * 34.0f;
            sfxThrow();
          }
        } else {
          foes[i].y = foes[i].baseY + sinf(now / 1400.0f) * 10.0f; // asleep: gentle bob
        }
      }
      // touch = death (teeth everywhere, no stomping underwater)
      float rr = (foes[i].type == 3) ? 15.0f : 9.0f;
      float dx = cx - (foes[i].x + 8), dy = pcy - (foes[i].y + 7);
      if (dx * dx + dy * dy < rr * rr) {
        killPlayer(now); if (state != PLAYING) return;
      }
    }
  }
  // --- Sergeant Black CHASE: flees, spins to volley, dashes clear ---
  if (bossMode && boss.alive) {
    float bcx = boss.x + 9;
    bool enraged = (boss.hp * 2 <= boss.maxHP);
    float fleeSpd = runSpeed - 0.8f; // Frank always gains on him
    if (boss.bstate == 0) { // FLEE: sprint right, Frank closes in
      boss.dir = 1;
      float gap = bcx - cx;
      float spd = (gap > 260) ? 0.5f : fleeSpd; // rubber-band: dawdle when far
      float bnx = boss.x + spd;
      if (!collide(bnx, boss.y, 18, 26)) boss.x = bnx;
      if ((gap < 150 && gap > 0) || now > boss.shootT0) {
        boss.bstate = 1; // spin & volley!
        boss.burstLeft = enraged ? 3 : 2;
        boss.burstT0 = now + 350;
      }
    } else if (boss.bstate == 1) { // VOLLEY: stand, face Frank, aimed bursts
      boss.dir = (cx < bcx) ? -1 : 1;
      if (boss.burstLeft > 0 && now > boss.burstT0 && now - boss.hurtT0 > 400) {
        boss.burstLeft--;
        boss.burstT0 = now + 320;
        for (auto &s : spears) {
          if (s.active) continue;
          s.active = true;
          s.x = bcx + boss.dir * 11; s.y = boss.y + 15;
          s.vx = boss.dir * (3.0f + level * 0.1f);
          break;
        }
        sfxThrow();
      }
      if (boss.burstLeft <= 0 && now > boss.burstT0 + 100) {
        boss.bstate = 2; // dash away!
        boss.burstT0 = now;
      }
    } else { // DASH: burst of speed to reopen the gap
      boss.dir = 1;
      float bnx = boss.x + runSpeed + 1.2f;
      if (!collide(bnx, boss.y, 18, 26)) boss.x = bnx;
      if (now - boss.burstT0 > 800) {
        boss.bstate = 0;
        boss.shootT0 = now + (enraged ? 1800 : 2600);
      }
    }
    // gravity + landing (flat chase, safety net)
    boss.vy += 0.5f; if (boss.vy > 7) boss.vy = 7;
    float bny = boss.y + boss.vy;
    if (!collide(boss.x, bny, 18, 26)) { boss.y = bny; }
    else {
      if (boss.vy > 0) boss.y = (int)((bny + 26) / TILE) * TILE - 26 - 0.01f;
      else boss.y = ((int)(bny / TILE) + 1) * TILE + 0.01f;
      boss.vy = 0;
    }
    // wrap: chase continues around the loop
    if (boss.x > mapW * TILE - 8) boss.x = px + 220;
    // touch?
    if (cx > boss.x - 2 && cx < boss.x + 20 && feet > boss.y && py < boss.y + 26) {
      if (vy > 1.0f && feet - boss.y < 11 && now - boss.hurtT0 > 800) { // HEAD STOMP!
        boss.hp--; boss.hurtT0 = now;
        vy = -8.2f;
        score += 200; if (score > hiScore) hiScore = score;
        sfxStomp();
        if (boss.hp <= 0) {
          boss.alive = false;
          score += 1000; if (score > hiScore) hiScore = score;
          state = LEVEL_CLEAR; stateChangeMs = now;
          sfxWin();
          return;
        }
        boss.bstate = 2; boss.burstT0 = now; // stomped: he bolts!
      } else if (now - boss.hurtT0 > 800) {
        killPlayer(now); if (state != PLAYING) return;
      }
    }
  }

  for (auto &s : spears) {
    if (!s.active) continue;
    s.x += s.vx;
    if (s.x < camX - 20 || s.x > camX + W + 20) { s.active = false; continue; }
    if (solidPx(s.x, s.y)) { s.active = false; continue; }
    if (s.x > px - 1 && s.x < px + PW + 1 && s.y > py && s.y < py + PH) {
      s.active = false;
      killPlayer(now); if (state != PLAYING) return;
    }
  }

  // --- boulder spawners ---
  for (int i = 0; i < nSpawners; i++) {
    float sx = spawners[i].tx * TILE + 8;
    if (now > spawners[i].nextT0 && cx > sx - 260 && cx < sx - 20) {
      spawners[i].nextT0 = now + max(1800, 3800 - level * 250);
      for (auto &r : rocks) {
        if (r.active) continue;
        r.active = true;
        r.x = sx; r.y = spawners[i].ty * TILE;
        r.vx = -(1.3f + level * 0.15f); r.vy = 0;
        break;
      }
      break; // one spawn per frame max
    }
  }
  // --- boulders: roll left with gravity, crush on touch ---
  for (auto &r : rocks) {
    if (!r.active) continue;
    r.vy += 0.5f; if (r.vy > 7) r.vy = 7;
    float rnx = r.x + r.vx;
    if (!collide(rnx - 7, r.y - 7, 14, 14)) r.x = rnx;
    else if (!collide(rnx - 7, r.y - 7 + 10, 14, 14)) { r.x = rnx; r.y += 10; r.vy = 0; } // tumble down a step
    else { r.vx = -r.vx * 0.5f; } // true wall: bounce (loses steam)
    float rny = r.y + r.vy;
    if (!collide(r.x - 7, rny - 7, 14, 14)) r.y = rny;
    else { r.y = (int)((rny + 7) / TILE) * TILE - 7 - 0.01f; r.vy = 0; }
    if (r.x < camX - 30 || r.x > camX + W + 40 || r.y > ROWS * TILE + 20) { r.active = false; continue; }
    float dx = cx - r.x, dy = (py + PH / 2) - r.y;
    if (dx * dx + dy * dy < 12 * 12) {
      // stomp a slow boulder? no — boulders always hurt. Jump over!
      killPlayer(now); if (state != PLAYING) return;
    }
  }

  // --- exit door / treasure chest ---
  // (swim exit: touch the sunlit cave mouth at any depth)
  // NOTE: chest (X) can sit high on a hill (LV2) so it needs an AABB check
  // around its own tile. Doors (E) stay near the floor.
  bool reached;
  if (boss.alive) reached = false; // boss levels end by KO, not by exit
  else if (swimMode) reached = (px + PW > exitX && px < exitX + 16 && py + PH > 144 && py < 176);
  else if (exitIsChest) reached = (px + PW > exitX && px < exitX + 16 && py + PH > exitY && py < exitY + 24);
  else reached = (cx > exitX && cx < exitX + 24 && feet > ROWS * TILE - 70);
  if (reached) {
    if (exitIsChest) {
      score += 1000; // 100 coins!
      if (score > hiScore) hiScore = score;
    } else {
      score += 500; if (score > hiScore) hiScore = score;
    }
    state = LEVEL_CLEAR; stateChangeMs = now;
    sfxWin();
    return;
  }
}

// ---------- Draw ----------
static void drawBrick(int sx, int sy, bool dark) {
  uint16_t base = dark ? col(120, 70, 35) : col(170, 110, 60);
  uint16_t hi = dark ? col(150, 95, 50) : col(205, 145, 85);
  uint16_t mort = col(60, 35, 20);
  R(sx, sy, 16, 16, base);
  R(sx, sy, 16, 2, hi);
  R(sx, sy + 7, 16, 2, mort);
  R(sx + 7, sy, 2, 7, mort);
  R(sx + 3, sy + 9, 2, 7, mort);
  R(sx + 11, sy + 9, 2, 7, mort);
}

// Frank: short stumpy Mario-style explorer. sx,sy = screen top-left of 12x20 box.
static void drawFrank(int sx, int sy, bool runFrame) {
  uint16_t hat = col(139, 90, 30), hatD = col(95, 60, 20);
  uint16_t skin = col(255, 200, 150);
  uint16_t shirt = col(212, 190, 138); // beige
  uint16_t shorts = col(178, 158, 108);
  uint16_t boots = col(120, 70, 30);
  // cowboy hat: dome + wide brim
  R(sx + 2, sy, 8, 4, hat);
  R(sx + 3, sy + 3, 6, 1, hatD);
  R(sx - 1, sy + 4, 14, 3, hat);
  R(sx - 1, sy + 6, 14, 1, hatD);
  // face (looking right: nose pixel)
  R(sx + 2, sy + 7, 8, 4, skin);
  R(sx + 10, sy + 8, 2, 2, skin);
  P(sx + 8, sy + 8, TFT_BLACK); // eye
  // beige shirt + arms
  R(sx + 1, sy + 11, 10, 4, shirt);
  R(sx - 1, sy + 11, 2, 3, skin); // back arm
  R(sx + 11, sy + 11, 2, 3, skin); // front arm
  // beige shorts
  R(sx + 2, sy + 15, 8, 2, shorts);
  // stumpy boots: run cycle
  if (runFrame) {
    R(sx + 1, sy + 17, 4, 3, boots);
    R(sx + 7, sy + 17, 4, 2, boots);
  } else {
    R(sx + 1, sy + 17, 4, 2, boots);
    R(sx + 7, sy + 17, 4, 3, boots);
  }
}

static void drawFoe(int sx, int sy, bool throwing) {
  uint16_t skin = col(110, 75, 50), band = col(200, 40, 40);
  uint16_t cloth = col(60, 90, 60);
  C(sx + 6, sy + 3, 3, skin); // head
  R(sx + 3, sy + 2, 6, 2, band); // headband
  R(sx + 2, sy + 6, 8, 5, cloth); // body
  R(sx + 3, sy + 11, 2, 3, skin);
  R(sx + 7, sy + 11, 2, 3, skin);
  if (throwing) L(sx + 10, sy + 7, sx + 18, sy + 5, col(180, 140, 90)); // spear raised
}

// Sea pack (swim levels). sx,sy = screen pos of 16x14 box top-left.
static void drawSnake(int sx, int sy, uint32_t now) {
  uint16_t g = col(60, 180, 80), gd = col(30, 120, 50);
  for (int i = 0; i < 3; i++) {
    int off = (int)(sinf((now + i * 250) / 220.0f) * 3);
    C(sx + 3 + i * 5, sy + 7 + off, 3, i ? g : gd);
  }
  P(sx + 2, sy + 6, TFT_BLACK); // eye
  L(sx + 1, sy + 9, sx - 2, sy + 11, TFT_RED); // tongue
}
static void drawCuda(int sx, int sy, bool charging) {
  uint16_t sil = charging ? col(230, 230, 240) : col(170, 180, 190);
  R(sx, sy + 5, 15, 4, sil); // torpedo body
  L(sx + 15, sy + 7, sx + 19, sy + 4, sil); // tail
  L(sx + 15, sy + 7, sx + 19, sy + 10, sil);
  R(sx - 3, sy + 6, 3, 2, TFT_WHITE); // teeth
  P(sx + 2, sy + 6, TFT_BLACK); // eye
  R(sx + 6, sy + 2, 3, 3, col(120, 130, 140)); // fin
}
// Poison frog (12x10 box) + circling bat (14x9 box).
static void drawFrog(int sx, int sy, bool hopping) {
  uint16_t g = col(70, 190, 70), gd = col(40, 130, 40), spot = col(20, 90, 30);
  int squash = hopping ? 0 : 1;
  C(sx + 6, sy + 6 + squash, 5, g); // round body
  C(sx + 4, sy + 2, 2, g); C(sx + 8, sy + 2, 2, g); // eye bumps
  P(sx + 4, sy + 2, TFT_BLACK); P(sx + 8, sy + 2, TFT_BLACK);
  P(sx + 5, sy + 6, spot); P(sx + 8, sy + 7, spot); P(sx + 3, sy + 8, spot);
  R(sx + 4, sy + (hopping ? 6 : 8), 4, 1, TFT_RED); // mouth
  if (hopping) { R(sx, sy + 7, 3, 2, gd); R(sx + 9, sy + 7, 3, 2, gd); } // kicked legs
}
static void drawBat(int sx, int sy, uint32_t now) {
  uint16_t b = col(90, 70, 120), bd = col(60, 45, 85);
  bool flap = (now / 120) & 1;
  if (flap) { L(sx, sy + 4, sx - 6, sy - 2, b); L(sx + 14, sy + 4, sx + 20, sy - 2, b); }
  else { L(sx, sy + 4, sx - 6, sy + 8, b); L(sx + 14, sy + 4, sx + 20, sy + 8, b); }
  C(sx + 7, sy + 4, 4, bd); // body
  C(sx + 7, sy + 2, 2, b); // head
  P(sx + 6, sy + 2, TFT_RED); P(sx + 8, sy + 2, TFT_RED); // glowing eyes
}
// Sergeant Black: 18x26 soldier in black with a gun. sx,sy = screen top-left.
static void drawBoss(int sx, int sy, int dir, bool hurt, uint32_t now) {
  uint16_t uni = hurt ? TFT_WHITE : col(25, 25, 30);
  uint16_t trim = col(180, 40, 40);
  uint16_t skin = col(220, 170, 130);
  int step = ((now / 150) & 1) ? 2 : 0;
  // beret + head
  R(sx + 3, sy, 12, 3, hurt ? TFT_WHITE : col(10, 10, 12));
  R(sx + 4, sy + 3, 10, 6, skin);
  P(sx + (dir > 0 ? 11 : 6), sy + 5, TFT_BLACK); // eye
  // black uniform + red trim
  R(sx + 2, sy + 9, 14, 9, uni);
  R(sx + 2, sy + 12, 14, 1, trim);
  R(sx, sy + 10, 3, 5, skin); // rear arm
  // gun arm pointing at Frank
  int gx = (dir > 0) ? sx + 16 : sx - 8;
  R(sx + (dir > 0 ? 12 : 3), sy + 11, 5, 3, uni);
  R(gx, sy + 11, 9, 3, col(80, 80, 90)); // gun
  R(gx + (dir > 0 ? 7 : 0), sy + 11, 2, 3, TFT_BLACK); // muzzle
  // marching boots
  R(sx + 3, sy + 18 + step, 5, 8 - step, uni);
  R(sx + 10, sy + 18 + (2 - step), 5, 6 + step, uni);
  R(sx + 3, sy + 24, 5, 2, trim);
  R(sx + 10, sy + 24, 5, 2, trim);
}
static void drawShark(int sx, int sy, uint32_t now) {  uint16_t g = col(140, 150, 165), gd = col(95, 105, 120);
  bool chomp = (now / 250) & 1;
  C(sx + 15, sy + 7, 8, g); // head
  R(sx + 14, sy + 1, 20, 12, g); // body
  L(sx + 34, sy + 7, sx + 42, sy + 1, gd); // tail
  L(sx + 34, sy + 7, sx + 42, sy + 13, gd);
  L(sx + 20, sy + 1, sx + 26, sy - 6, gd); // dorsal fin
  R(sx + 8, sy + (chomp ? 8 : 9), 9, 2, TFT_WHITE); // teeth
  P(sx + 10, sy + 5, TFT_BLACK); // eye
  C(sx + 10, sy + 5, 1, TFT_RED);
}

static void draw() {
  if (!useSprite) lcd.startWrite();
  if (swimMode) {
    // sunlit water: bright top fading to deep bottom
    GFX->fillScreen(col(8, 55, 110)); // full clear first (shift leaves edge cols)
    R(0, 0, W, 70, col(20, 110, 170));
    R(0, 70, W, 70, col(12, 80, 140));
    R(0, 140, W, 100, col(8, 55, 110));
    // light rays from the surface
    for (int i = 0; i < 4; i++) {
      int rx = (i * 97 + 40 - (int)(camX / 3) % 320 + 320) % 340 - 10;
      L(rx, 0, rx - 24, 120, col(60, 160, 200));
    }
  } else {
    // tunnel backdrop: dark rock with subtle strata
    GFX->fillScreen(col(18, 14, 26));
  }
  uint32_t now = millis();

  // HUD
  GFX->setTextSize(1);
  GFX->setTextColor(TFT_WHITE, TFT_BLACK);
  GFX->setCursor((4) + SCREEN_OX, 6);
  GFX->printf("SC:%05d HI:%05d", score, hiScore);
  GFX->setCursor((W - 110) + SCREEN_OX, 6);
  GFX->printf("LV:%d LIVES:%d", level, lives);
  L(0, HUD_H - 3, W, HUD_H - 3, col(60, 60, 60));

  if (state == TITLE) {
    // Intro art: Frank_Tunnel_Run.jpg (320x240, PROGMEM JPEG)
    // Banner at TOP so it never covers the painted title at the bottom.
    GFX->drawJpg(INTRO_JPG, INTRO_JPG_LEN, 0, 0, W, H);
    // Blinking TAP TO START banner with readable backdrop
    if ((now / 500) & 1) {
      GFX->fillRect(70, 8, 180, 20, TFT_BLACK);
      GFX->setTextSize(2);
      GFX->setTextColor(TFT_YELLOW, TFT_BLACK);
      GFX->setCursor(78, 12);
      GFX->print("TAP TO START");
    } else {
      GFX->fillRect(70, 8, 180, 20, TFT_BLACK);
      GFX->setTextSize(1);
      GFX->setTextColor(TFT_WHITE, TFT_BLACK);
      GFX->setCursor(82, 15);
      GFX->print("Frank auto-runs! TAP=jump");
    }
    GFX->setTextSize(1);
    GFX->setTextColor(TFT_CYAN, TFT_BLACK);
    GFX->setCursor((W / 2 - 105) + SCREEN_OX, 30);
    GFX->print("TEST: TAP ZONE=LEVEL");
    if (useSprite) fb.pushSprite(0, 0); else lcd.endWrite();
    return;
  }

  int cam = (int)camX;
  int tx0 = cam / TILE, tx1 = (cam + W) / TILE + 1;

  // tiles
  for (int ty = 0; ty < mapRows; ty++) {
    for (int tx = tx0; tx <= tx1; tx++) {
      char t = tileAt(tx, ty);
      int sx = tx * TILE - cam, sy = ty * TILE;
      if (t == '#' || t == '=') {
        drawBrick(sx, sy, t == '=');
      } else if (t == 'F') {
        if (swimMode) {
          // thermal vent: dark mound + rising bubble column (deadly!)
          bool f = (now / 150) & 1;
          R(sx + 1, sy + 10, 14, 6, col(50, 40, 45));
          R(sx + 5, sy + (f ? 0 : 3), 3, 9, col(150, 220, 255));
          R(sx + 9, sy + (f ? 4 : 1), 2, 7, col(255, 120, 80));
        } else {
          bool f = (now / 150) & 1;
          R(sx + 1, sy + 6, 14, 10, TFT_RED);
          R(sx + 3, sy + (f ? 2 : 5), 10, 9, TFT_ORANGE);
          R(sx + 6, sy + (f ? 6 : 3), 4, 7, TFT_YELLOW);
        }
      } else if (t == 'E') {
        // exit door (draw once: E tile + tile above)
        if (tileAt(tx, ty - 1) != 'E') {
          if (swimMode) {
            // sunlit cave mouth
            R(sx - 2, sy, 20, 32, col(90, 70, 50));
            R(sx, sy + 3, 16, 29, col(120, 220, 235));
            R(sx + 3, sy + 6, 4, 20, col(200, 250, 250));
          } else {
          R(sx - 2, sy, 20, 32, col(90, 55, 25));
          R(sx, sy + 3, 16, 29, TFT_BLACK);
          GFX->setTextSize(1);
          GFX->setTextColor(TFT_YELLOW, TFT_BLACK);
          GFX->setCursor((sx - 1) + SCREEN_OX, sy + 5);
          GFX->print("EXIT");
          }
        }
      } else if (t == 'X') {
        // treasure chest: wooden box, gold bands, glowing lock
        bool glow = (now / 300) & 1;
        R(sx + 1, sy + 5, 14, 9, col(110, 70, 30));
        R(sx + 1, sy + 2, 14, 4, col(150, 100, 45));
        R(sx + 7, sy + 2, 3, 12, col(230, 190, 60));
        R(sx + 1, sy + 5, 14, 1, col(70, 45, 20));
        C(sx + 8, sy + 9, glow ? 3 : 2, TFT_YELLOW);
        if (glow) { P(sx + 3, sy + 1, TFT_YELLOW); P(sx + 13, sy, TFT_YELLOW); }
      }
    }
  }
  // ceiling torches, tunnels only (none underwater)
  if (!swimMode) for (int tx = tx0; tx <= tx1; tx++) {
    if (tx % 9 == 4 && tileAt(tx, 0) == '#') {
      int sx = tx * TILE - cam + 7;
      bool f = (now / 130) & 1;
      R(sx - 1, 16, 3, 8, col(100, 65, 30));
      C(sx, 14 - (f ? 1 : 0), 3, TFT_ORANGE);
      C(sx, 14, 2, TFT_YELLOW);
    }
  }

  // coins (pearls underwater)
  for (int i = 0; i < nCoins; i++) {
    if (coins[i].taken) continue;
    int sx = coins[i].tx * TILE + 8 - cam;
    int sy = coins[i].ty * TILE + 8 + (int)(sinf((now + i * 300) / 400.0f) * 2);
    if (sx < -10 || sx > W + 10) continue;
    if (swimMode) {
      C(sx, sy, 5, col(220, 235, 245));
      C(sx - 1, sy - 1, 2, TFT_WHITE);
    } else {
      C(sx, sy, 5, col(230, 180, 40));
      C(sx, sy, 3, TFT_YELLOW);
    }
  }

  // tribesmen / sea pack
  for (int i = 0; i < nFoes; i++) {
    if (!foes[i].alive) continue;
    int sx = (int)foes[i].x - cam, sy = (int)foes[i].y;
    if (sx < -46 || sx > W + 20) continue;
    if (foes[i].type == 1) drawSnake(sx, sy, now);
    else if (foes[i].type == 2) drawCuda(sx, sy, foes[i].vx != 0);
    else if (foes[i].type == 3) drawShark(sx - 14, sy - 2, now);
    else if (foes[i].type == 4) drawFrog(sx, sy, foes[i].vy != 0);
    else if (foes[i].type == 5) drawBat(sx, sy, now);
    else drawFoe(sx, sy, (now - foes[i].t0) < 400);
  }

  // spears (gun tracers on boss levels)
  for (auto &s : spears) {
    if (!s.active) continue;
    int sx = (int)s.x - cam, sy = (int)s.y;
    if (bossMode) {
      int d = (s.vx > 0) ? 1 : -1;
      L(sx - 7 * d, sy, sx + 7 * d, sy, TFT_YELLOW);
      C(sx + 7 * d, sy, 2, TFT_WHITE);
    } else {
      int d = (s.vx > 0) ? 1 : -1;
      L(sx - 6 * d, sy, sx + 6 * d, sy, col(190, 150, 100));
      R(sx + 6 * d - 1, sy - 1, 3, 3, TFT_LIGHTGREY);
    }
  }

  // boulders
  for (auto &r : rocks) {
    if (!r.active) continue;
    int sx = (int)r.x - cam, sy = (int)r.y;
    if (sx < -20 || sx > W + 20) continue;
    C(sx, sy, 8, col(130, 130, 140));
    C(sx - 2, sy - 2, 5, col(160, 160, 170));
    int a = (int)(r.x / 4) % 4;
    int ox[4] = {4, 0, -4, 0}, oy[4] = {0, 4, 0, -4};
    C(sx + ox[a], sy + oy[a], 2, col(90, 90, 100));
  }

  // Sergeant Black + HP pips
  if (bossMode && boss.alive) {
    int sx = (int)boss.x - cam, sy = (int)boss.y;
    if (sx > -30 && sx < W + 30)
      drawBoss(sx, sy, boss.dir, (now - boss.hurtT0) < 300, now);
    for (int i = 0; i < boss.maxHP; i++) {
      int pipx = W / 2 - boss.maxHP * 7 + i * 14;
      if (i < boss.hp) R(pipx, HUD_H + 2, 12, 5, TFT_RED);
      else R(pipx, HUD_H + 2, 12, 5, col(80, 20, 20));
    }
  }

  // Frank (blink while spawn-protected; fast kick + bubbles when swimming)
  if (now - protectT0 < 2500 && ((now / 150) & 1)) {
  } else {
    int fsx = (int)px - cam, fsy = (int)py;
    if (swimMode) {
      drawFrank(fsx, fsy, (now / 90) & 1);
      int b = (now / 400) % 3;
      OC(fsx + 10, fsy - 2 - b * 3, 1 + (b == 2), col(180, 230, 250));
    } else {
      drawFrank(fsx, fsy, ((now / 120) & 1) && grounded);
    }
  }

  if (swimMode) {
    // seaweed on the floor + rising ambient bubbles
    for (int tx = tx0; tx <= tx1; tx++) {
      if (tx % 5 == 2 && tileAt(tx, 11) == '#') {
        int sx = tx * TILE - cam + 8;
        int sway = (int)(sinf((now + tx * 400) / 500.0f) * 3);
        L(sx, 176, sx + sway, 158, col(30, 140, 70));
        L(sx + sway, 158, sx - sway, 146, col(40, 170, 90));
      }
    }
    for (int i = 0; i < 10; i++) {
      int bx = (i * 53 + (int)(camX / 2)) % (W + 20) - 10;
      int by = H - (int)((now / 12 + i * 67) % (H + 20)) + 10;
      OC(bx, by, 1, col(150, 210, 240));
    }
  }

  // progress bar to exit
  {
    float prog = (px + PW / 2) / exitX;
    if (prog < 0) prog = 0; if (prog > 1) prog = 1;
    int bw = 100, bx = W - bw - 4 - 34, by = H - 8;
    R(bx - 1, by - 1, bw + 2, 5, col(60, 60, 60));
    R(bx, by, (int)(bw * prog), 3, TFT_GREEN);
  }

  if (state == PLAYING && now - protectT0 < 2500) {
    GFX->setTextSize(1);
    GFX->setTextColor(TFT_CYAN, TFT_BLACK);
    int idx = (level - 1) % NUM_LEVELS;
    GFX->setCursor((W / 2 - 70) + SCREEN_OX, 40);
    GFX->print(LEVEL_NAMES[idx]);
    GFX->setCursor((W / 2 - 55) + SCREEN_OX, 52);
    if (swimMode) GFX->print("DRAG UP/DOWN!");
    else GFX->print("TAP TO JUMP!");
  }
  if (state == LEVEL_CLEAR) {
    int idx = (level - 1) % NUM_LEVELS;
    bool isFinal = (idx == NUM_LEVELS - 1);
    if (!isFinal) {
      // Mid-game clear: Frank_Level_Cleared.jpg fullscreen + banner
      GFX->drawJpg(CLEARED_JPG, CLEARED_JPG_LEN, 0, 0, W, H);
      GFX->fillRect(30, 188, 260, 34, TFT_BLACK);
      GFX->setTextSize(2);
      GFX->setTextColor(TFT_YELLOW, TFT_BLACK);
      GFX->setCursor(40, 192);
      GFX->printf("%s CLEAR!", LEVEL_NAMES[idx]);
      GFX->setTextSize(1);
      GFX->setTextColor(TFT_WHITE, TFT_BLACK);
      GFX->setCursor(110, 210);
      GFX->print("TAP FOR NEXT");
    } else {
      // Final level complete: Frank_Wins.jpg fullscreen + victory banner
      GFX->drawJpg(WINS_JPG, WINS_JPG_LEN, 0, 0, W, H);
      GFX->fillRect(30, 184, 260, 44, TFT_BLACK);
      GFX->setTextSize(2);
      GFX->setTextColor(TFT_YELLOW, TFT_BLACK);
      GFX->setCursor(62, 188);
      GFX->print("YOU WIN!");
      GFX->setTextSize(1);
      GFX->setTextColor(TFT_WHITE, TFT_BLACK);
      GFX->setCursor(78, 208);
      GFX->printf("SCORE %05d TAP TO REPLAY", score);
    }
  }
  if (state == GAME_OVER) {
    GFX->setTextSize(2);
    GFX->setTextColor(TFT_RED, TFT_BLACK);
    GFX->setCursor((W / 2 - 62) + SCREEN_OX, H / 2 - 20);
    GFX->print("GAME OVER");
    GFX->setTextSize(1);
    GFX->setTextColor(TFT_WHITE, TFT_BLACK);
    GFX->setCursor((W / 2 - 60) + SCREEN_OX, H / 2 + 8);
    GFX->printf("SCORE %05d LV%d", score, level);
    GFX->setCursor((W / 2 - 60) + SCREEN_OX, H / 2 + 22);
    GFX->print("TAP TO RETRY");
  }

  if (useSprite) fb.pushSprite(0, 0); else lcd.endWrite();
}

// ---------- Arduino ----------
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\nFRANK TUNNEL RUN boot");
  ledcSetup(0, 2000, 8);
  ledcAttachPin(SPEAKER_PIN, 0);
  ledcWriteTone(0, 0);
  randomSeed(esp_random());

  lcd.init();
  Serial.printf("after init w=%d h=%d\n", lcd.width(), lcd.height());
  lcd.setRotation(0); // ILI9342 landscape-native: rot 0 = 320x240
  lcd.setBrightness(200);
  lcd.fillScreen(TFT_RED);
  delay(400);
  lcd.fillScreen(TFT_GREEN);
  delay(400);
  lcd.fillScreen(TFT_BLUE);
  delay(400);
  lcd.fillScreen(TFT_BLACK);

  fb.setColorDepth(8);
  void* ptr = fb.createSprite(W, H);
  useSprite = (ptr != nullptr);
  GFX = useSprite ? (lgfx::LovyanGFX*)&fb : (lgfx::LovyanGFX*)&lcd;
  Serial.printf("sprite=%p useSprite=%d heap=%u maxblock=%u\n", ptr, (int)useSprite,
                ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  tmap = LV1; mapRows = LV1_ROWS; mapW = 0;
  for (int r = 0; r < mapRows; r++) mapW = max(mapW, (int)strlen(LV1[r]));
  state = TITLE;
  lastFrame = millis();
}

void loop() {
  updateTouch();
  updateGame();
  updateSound();
  uint32_t now = millis();
  if (now - lastFrame >= 33) { // ~30fps
    lastFrame = now;
    draw();
  }
  delay(1);
}

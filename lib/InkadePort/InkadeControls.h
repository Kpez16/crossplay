#pragma once
// Touch-pad geometry and drawing for the Game Boy player on the T5 S3 Pro.
//
// Derived from InkDeck's InkBoyUi.h (AGPL-3.0-or-later; see third_party/inkdeck):
// same constants and hit shapes, so the pad sits where it does there. The drawing
// is rewritten to plot straight into the panel's own 1-bit buffer through a
// callback, with a built-in 5x7 font for the few labels, so it needs no GFX
// library.
//
// Coordinates here are InkDeck's portrait canvas (524 x 944, inside the 8 px
// bezel inset); LilygoLayout::mapTouch turns a native touch into them.

#include <stdint.h>
#include <string.h>

#include "GameViewport.h"
#include "LilygoLayout.h"

namespace InkadeControls {

constexpr int DPAD_X = 120, DPAD_Y = 674, DPAD_HALF = 88, DPAD_ARM = 29;
constexpr int A_X = 443, A_Y = 630, B_X = 338, B_Y = 681, AB_RADIUS = 43;
constexpr int SELECT_X = 174, START_X = 285, SMALL_Y = 810;
constexpr int SMALL_LENGTH = 82, SMALL_HEIGHT = 26;
constexpr int BEZEL_X = 10, BEZEL_Y = 60, BEZEL_W = 504, BEZEL_H = 466;
constexpr int GRILLE_X = 355, GRILLE_Y = 838, GRILLE_STEP_X = 24, GRILLE_STEP_Y = 10;
constexpr int GRILLE_DX = 36, GRILLE_DY = 66;
constexpr int TOP_BAR_H = 60;  // a tap here leaves the game

// Game Boy button bits, as the core wants them.
constexpr uint8_t BTN_A = 0x01, BTN_B = 0x02, BTN_SELECT = 0x04, BTN_START = 0x08;
constexpr uint8_t BTN_RIGHT = 0x10, BTN_LEFT = 0x20, BTN_UP = 0x40, BTN_DOWN = 0x80;

inline bool circleHit(int x, int y, int cx, int cy, int r) {
  return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r;
}
// A capsule rotated -25 degrees (the Select/Start shape). Fixed point, no trig.
inline bool capsuleHit(int x, int y, int cx, int cy, int length, int height, int pad = 0) {
  const int dx = x - cx, dy = y - cy;
  const int u = (906 * dx - 423 * dy) / 1000, v = (423 * dx + 906 * dy) / 1000;
  const int segment = (length - height) / 2, r = height / 2 + pad;
  const int end = u < -segment ? -segment : u > segment ? segment : u;
  return (u - end) * (u - end) + v * v <= r * r;
}
inline bool roundRectHit(int x, int y, int left, int top, int w, int h, int r) {
  if (x < left || x >= left + w || y < top || y >= top + h) return false;
  const int cx = x < left + r ? left + r : x >= left + w - r ? left + w - r - 1 : x;
  const int cy = y < top + r ? top + r : y >= top + h - r ? top + h - r - 1 : y;
  return circleHit(x, y, cx, cy, r);
}

// Which game buttons a touch at canvas (x, y) presses. The whole D-pad square
// supports diagonals.
inline uint8_t buttonsAt(int x, int y) {
  uint8_t buttons = 0;
  if (x >= DPAD_X - DPAD_HALF && x <= DPAD_X + DPAD_HALF && y >= DPAD_Y - DPAD_HALF && y <= DPAD_Y + DPAD_HALF) {
    if (x < DPAD_X - DPAD_ARM) buttons |= BTN_LEFT;
    if (x > DPAD_X + DPAD_ARM) buttons |= BTN_RIGHT;
    if (y < DPAD_Y - DPAD_ARM) buttons |= BTN_UP;
    if (y > DPAD_Y + DPAD_ARM) buttons |= BTN_DOWN;
  }
  if (circleHit(x, y, A_X, A_Y, AB_RADIUS + 10)) buttons |= BTN_A;
  if (circleHit(x, y, B_X, B_Y, AB_RADIUS + 10)) buttons |= BTN_B;
  if (capsuleHit(x, y, SELECT_X, SMALL_Y, SMALL_LENGTH, SMALL_HEIGHT, 14)) buttons |= BTN_SELECT;
  if (capsuleHit(x, y, START_X, SMALL_Y, SMALL_LENGTH, SMALL_HEIGHT, 14)) buttons |= BTN_START;
  return buttons;
}
inline bool inTopBar(int x, int y) { return y >= 0 && y < TOP_BAR_H && x >= 0; }
// A tap on the picture asks for a clean redraw (ghosting recovery).
inline bool inGameWindow(int x, int y) {
  using namespace GameViewport;
  return x >= X && x < X + W && y >= Y && y < Y + H;
}

// 5x7 glyphs (columns, bit 0 = top) for the letters the labels use.
inline const uint8_t* glyph(char c) {
  static const uint8_t A[5] = {0x7E, 0x11, 0x11, 0x11, 0x7E}, B[5] = {0x7F, 0x49, 0x49, 0x49, 0x36},
                       C[5] = {0x3E, 0x41, 0x41, 0x41, 0x22}, E[5] = {0x7F, 0x49, 0x49, 0x49, 0x41},
                       I[5] = {0x00, 0x41, 0x7F, 0x41, 0x00}, L[5] = {0x7F, 0x40, 0x40, 0x40, 0x40},
                       R[5] = {0x7F, 0x09, 0x19, 0x29, 0x46}, S[5] = {0x46, 0x49, 0x49, 0x49, 0x31},
                       T[5] = {0x01, 0x01, 0x7F, 0x01, 0x01}, X[5] = {0x63, 0x14, 0x08, 0x14, 0x63};
  switch (c) {
    case 'A': return A;
    case 'B': return B;
    case 'C': return C;
    case 'E': return E;
    case 'I': return I;
    case 'L': return L;
    case 'R': return R;
    case 'S': return S;
    case 'T': return T;
    case 'X': return X;
    default: return nullptr;
  }
}

inline bool bayerBlack(int x, int y, int density) {
  static const uint8_t bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
  return bayer[y & 3][x & 3] < density;
}

// Draws the whole pad, bezel and labels. `plot(nativeX, nativeY)` blackens one
// pixel of the panel's native 960x540 frame; the buffer is expected to be white
// already. The game window itself is left alone.
template <typename Plot>
void draw(Plot plot) {
  using namespace GameViewport;
  auto px = [&](int x, int y) {
    const int nx = y + LilygoLayout::INSET, ny = LilygoLayout::PANEL_H - 1 - (x + LilygoLayout::INSET);
    if (static_cast<unsigned>(nx) >= static_cast<unsigned>(LilygoLayout::PANEL_W) ||
        static_cast<unsigned>(ny) >= static_cast<unsigned>(LilygoLayout::PANEL_H))
      return;
    plot(nx, ny);
  };
  auto glyphText = [&](const char* text, int scale, int cx, int top) {
    const int n = static_cast<int>(strlen(text));
    int left = cx - (n * 6 * scale - scale) / 2;
    for (int i = 0; i < n; ++i, left += 6 * scale) {
      const uint8_t* g = glyph(text[i]);
      if (!g) continue;
      for (int col = 0; col < 5; ++col)
        for (int row = 0; row < 7; ++row)
          if ((g[col] >> row) & 1)
            for (int dy = 0; dy < scale; ++dy)
              for (int dx = 0; dx < scale; ++dx) px(left + col * scale + dx, top + row * scale + dy);
    }
  };
  // The same text, scale 2, sheared by -25 degrees like the real buttons' labels.
  auto slanted = [&](const char* text, int cx, int cy) {
    const int len = static_cast<int>(strlen(text));
    const int width = len * 12;
    for (int dy = -30; dy <= 30; ++dy)
      for (int dx = -width; dx <= width; ++dx) {
        const int u = (906 * dx - 423 * dy) / 1000 + width / 2, v = (423 * dx + 906 * dy) / 1000 + 8;
        if (u < 0 || u >= width || v < 0 || v >= 16) continue;
        const uint8_t* g = glyph(text[u / 12]);
        const int gx = (u % 12) / 2, gy = v / 2;
        if (g && gx < 5 && gy < 7 && ((g[gx] >> gy) & 1)) px(cx + dx, cy + dy);
      }
  };

  // Bezel: dark gray, so no extra drive pulses are needed.
  for (int y = BEZEL_Y; y < BEZEL_Y + BEZEL_H; ++y)
    for (int x = BEZEL_X; x < BEZEL_X + BEZEL_W; ++x) {
      if (!roundRectHit(x, y, BEZEL_X, BEZEL_Y, BEZEL_W, BEZEL_H, 12)) continue;
      if (x >= GameViewport::X && x < GameViewport::X + W && y >= GameViewport::Y && y < GameViewport::Y + H) continue;
      if (bayerBlack(x, y, 12)) px(x, y);
    }
  for (int i = -1; i <= W; ++i) {
    px(GameViewport::X + i, GameViewport::Y - 1);
    px(GameViewport::X + i, GameViewport::Y + H);
  }
  for (int i = -1; i <= H; ++i) {
    px(GameViewport::X - 1, GameViewport::Y + i);
    px(GameViewport::X + W, GameViewport::Y + i);
  }

  // D-pad: a plus of two round bars.
  for (int y = DPAD_Y - DPAD_HALF; y <= DPAD_Y + DPAD_HALF; ++y)
    for (int x = DPAD_X - DPAD_HALF; x <= DPAD_X + DPAD_HALF; ++x)
      if (roundRectHit(x, y, DPAD_X - DPAD_HALF, DPAD_Y - DPAD_ARM, 2 * DPAD_HALF + 1, 2 * DPAD_ARM + 1, 5) ||
          roundRectHit(x, y, DPAD_X - DPAD_ARM, DPAD_Y - DPAD_HALF, 2 * DPAD_ARM + 1, 2 * DPAD_HALF + 1, 5))
        px(x, y);

  // A and B.
  for (int y = A_Y - AB_RADIUS; y <= A_Y + AB_RADIUS; ++y)
    for (int x = A_X - AB_RADIUS; x <= A_X + AB_RADIUS; ++x)
      if (circleHit(x, y, A_X, A_Y, AB_RADIUS)) px(x, y);
  for (int y = B_Y - AB_RADIUS; y <= B_Y + AB_RADIUS; ++y)
    for (int x = B_X - AB_RADIUS; x <= B_X + AB_RADIUS; ++x)
      if (circleHit(x, y, B_X, B_Y, AB_RADIUS)) px(x, y);
  glyphText("A", 3, A_X, A_Y + AB_RADIUS + 14);
  glyphText("B", 3, B_X, B_Y + AB_RADIUS + 14);

  // Select and Start: slanted capsules, light gray inside.
  auto capsule = [&](int cx, const char* text) {
    for (int y = SMALL_Y - 34; y <= SMALL_Y + 34; ++y)
      for (int x = cx - 50; x <= cx + 50; ++x) {
        if (!capsuleHit(x, y, cx, SMALL_Y, SMALL_LENGTH, SMALL_HEIGHT)) continue;
        const bool inner = capsuleHit(x, y, cx, SMALL_Y, SMALL_LENGTH - 4, SMALL_HEIGHT - 4);
        if (!inner || bayerBlack(x, y, 8)) px(x, y);
      }
    slanted(text, cx + 12, SMALL_Y + 33);
  };
  capsule(SELECT_X, "SELECT");
  capsule(START_X, "START");

  // Speaker grille: decoration only.
  for (int bar = 0; bar < 5; ++bar) {
    const int x0 = GRILLE_X + bar * GRILLE_STEP_X, y0 = GRILLE_Y - bar * GRILLE_STEP_Y;
    for (int off = -2; off <= 2; ++off)
      for (int t = 0; t <= GRILLE_DY; ++t) px(x0 + off + GRILLE_DX * t / GRILLE_DY, y0 + t);
  }

  // EXIT, top left; the whole top bar takes the tap.
  for (int y = 10; y < 50; ++y)
    for (int x = 12; x < 132; ++x)
      if (roundRectHit(x, y, 12, 10, 120, 40, 8) && !roundRectHit(x, y, 14, 12, 116, 36, 6)) px(x, y);
  glyphText("EXIT", 3, 72, 19);
}

}  // namespace InkadeControls

#pragma once

// Tak on screen. Freestanding builders over plain models.

#include "../ui/ToyboxScreen.h"
#include "TakCore.h"
#include "TakFlow.h"

namespace takui {

namespace fui = freeink::ui;

enum : fui::ActionId {
  ActionMenuRow = 1,
  ActionKind = 2,
  ActionPutBack = 3,
  ActionAgain = 4,
  ActionDone = 5,
  // The whole board, registered once. The squares themselves are found
  // arithmetically by cellAt(), from the same layout that drew them.
  ActionBoard = 6,
};

enum class MenuRow : int { Play = 0, TwoPlayers, Count };

struct MenuModel {
  int selected = -1;
};

struct BoardModel {
  // Owned by the Activity; the builder only reads them.
  const tak::Game* game = nullptr;
  const tak::Turn* turn = nullptr;
  tak::Opponent opponent = tak::Opponent::Computer;
  // The computer is working out its reply.
  bool thinking = false;
};

struct ResultModel {
  const tak::Game* game = nullptr;
  tak::Opponent opponent = tak::Opponent::Computer;
};

// Where the board sits, derived once from the device so drawing and hit-testing
// cannot disagree. A tall panel stacks the board over the controls; a wide one
// puts the controls beside it.
struct Layout {
  int16_t left = 0;
  int16_t top = 0;
  int16_t cell = 0;
  fui::Rect panel{};
  bool sideways = false;
};

Layout layoutFor(const fui::DeviceContext& device);

fui::Rect boardRect(const Layout& layout);
fui::Rect cellRect(const Layout& layout, int index);

// The square under a point, or false off the board. The inverse of cellRect().
bool cellAt(const Layout& layout, int x, int y, int& index);

void buildMenu(toybox::Screen& screen, const MenuModel& model);
void buildBoard(toybox::Screen& screen, const BoardModel& model);
void buildResult(toybox::Screen& screen, const ResultModel& model);

}  // namespace takui

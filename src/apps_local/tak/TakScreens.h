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
  ActionHowToNext = 7,
  // Setup: the value says which (3 or 5, 0 or 1, 0..2).
  ActionSize = 8,
  ActionSeat = 9,
  ActionLevel = 10,
  ActionStart = 11,
};

// The values the rows carry. CONTINUE is only on the list when a game was left
// part-played, and the rows after it do not change their values when it is not.
enum class MenuRow : int { Continue = 0, Play, TwoPlayers, HowTo, Count };

struct MenuModel {
  // The visible row to show chosen, or -1 for the first.
  int selected = -1;
  // A game was left part-played; CONTINUE heads the list, with this beneath it.
  bool hasSave = false;
  const char* saveNote = nullptr;
};

// The choices made before a game. The seat and the level only matter against
// the computer, and the screen leaves them out for two people.
struct SetupModel {
  bool vsComputer = true;
  int size = tak::kSize;
  tak::Color player = tak::Color::White;
  tak::Level level = tak::Level::Medium;
};

struct HowToModel {
  int page = 0;
};

int howToPages();

struct BoardModel {
  // Owned by the Activity; the builder only reads them.
  const tak::Game* game = nullptr;
  const tak::Turn* turn = nullptr;
  tak::Opponent opponent = tak::Opponent::Computer;
  // The colour the person holding the device plays against the computer.
  tak::Color player = tak::Color::White;
  // The computer is working out its reply.
  bool thinking = false;
};

struct ResultModel {
  const tak::Game* game = nullptr;
  tak::Opponent opponent = tak::Opponent::Computer;
  tak::Color player = tak::Color::White;
};

// Where the board sits, derived once from the device so drawing and hit-testing
// cannot disagree. A tall panel stacks the board over the controls; a wide one
// puts the controls beside it.
struct Layout {
  int16_t left = 0;
  int16_t top = 0;
  int16_t cell = 0;
  // Squares along an edge: 3 or 5.
  int16_t size = tak::kSize;
  fui::Rect panel{};
  bool sideways = false;
};

Layout layoutFor(const fui::DeviceContext& device, int size = tak::kSize);

fui::Rect boardRect(const Layout& layout);
fui::Rect cellRect(const Layout& layout, int index);

// The square under a point, or false off the board. The inverse of cellRect().
bool cellAt(const Layout& layout, int x, int y, int& index);

void buildMenu(toybox::Screen& screen, const MenuModel& model);
void buildSetup(toybox::Screen& screen, const SetupModel& model);
void buildHowTo(toybox::Screen& screen, const HowToModel& model);
void buildBoard(toybox::Screen& screen, const BoardModel& model);
void buildResult(toybox::Screen& screen, const ResultModel& model);

}  // namespace takui

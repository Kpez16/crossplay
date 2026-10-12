#pragma once

#include <memory>

#include "../../activities/Activity.h"
#include "../ui/ToyboxScreen.h"
#include "TakCore.h"
#include "TakFlow.h"
#include "TakScreens.h"

// Tak: build a road across the board, or hold the most flats when it fills.
//
// Against the device or passing it between two people. The rules are TakCore's
// and the computer is TakBrain's; this class owns only the screen, the tap
// flow and the clock. A lone player's whole turn is a handful of taps (see
// TakFlow.h), and the computer's reply is deferred one beat so the board is
// shown WITH the player's move and the word THINKING before the device goes
// quiet to work it out.
class TakActivity final : public Activity {
 public:
  TakActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) : Activity("Tak", renderer, mappedInput) {}
  ~TakActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  uint32_t surfaceMeaning() const override;

  void startGame(tak::Opponent who);
  void afterMove();
  void playComputer();
  void routeBoardTap(int x, int y);
  void showScreen(tak::Screen next);
  bool computersTurn() const;

  tak::Screen view = tak::Screen::Menu;
  tak::Game game;
  tak::Turn turn;
  tak::Opponent opponent = tak::Opponent::Computer;
  uint32_t rngState = 1;
  // Where the board was last drawn; a tap is resolved against this.
  takui::Layout layout;

  // The computer has been asked to move and will do so once `thinkAt` passes.
  bool thinking = false;
  uint32_t thinkAt = 0;

  int menuSelected = -1;
  bool interactionsReady = false;
  // Consumed by the next render(): the full blink instead of the fast refresh.
  bool flashOnNextPaint = false;
  int paintsSinceFlash = 0;
  toybox::Interactions interactions;
};

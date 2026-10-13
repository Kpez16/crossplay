#include "TakActivity.h"

#include <Logging.h>
#include <Memory.h>

#include "../../activities/ActivityManager.h"
#include "../../components/UITheme.h"
#include "../Shelf.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"
#include "TakBrain.h"

namespace {

namespace fui = freeink::ui;
namespace ui = takui;

// After this many board repaints with no full refresh, force one: pieces on a
// dithered grid ghost quickly.
constexpr int kFlashEvery = 12;

// How long the board is left showing THINKING before the computer starts, so
// the player's own move is on the panel first.
constexpr uint32_t kThinkDelayMs = 250;

// The brain's scratch list. Static, not a local: kMaxMoves entries are a large
// fraction of the render task's stack.
tak::Move gScratch[tak::kMaxMoves];

constexpr tak::PieceKind kKinds[3] = {tak::PieceKind::Flat, tak::PieceKind::Wall, tak::PieceKind::Cap};

}  // namespace

std::unique_ptr<Activity> TakActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<TakActivity>(renderer, mappedInput);
}

void TakActivity::onEnter() {
  toybox::ensureFonts(renderer);
  tak::reset(game);
  tak::clearHand(turn);
  view = tak::Screen::Menu;
  thinking = false;
  menuSelected = -1;
  flashOnNextPaint = true;
  requestUpdate();
}

void TakActivity::onExit() { thinking = false; }

bool TakActivity::computersTurn() const {
  return opponent == tak::Opponent::Computer && game.toMove != player && !game.over;
}

void TakActivity::showScreen(const tak::Screen next) {
  view = next;
  thinking = false;
  tak::clearHand(turn);
  flashOnNextPaint = true;
  requestUpdate();
}

void TakActivity::openSetup(const tak::Opponent who) {
  opponent = who;
  showScreen(tak::Screen::Setup);
}

void TakActivity::startGame() {
  tak::reset(game, boardSize);
  tak::clearHand(turn);
  // Different every game, and never zero (the generator would stay there).
  rngState = (static_cast<uint32_t>(millis()) * 2654435761u) | 1u;
  paintsSinceFlash = 0;
  showScreen(tak::Screen::Board);
  // Playing Black means the computer opens.
  if (computersTurn()) {
    thinking = true;
    thinkAt = static_cast<uint32_t>(millis()) + kThinkDelayMs;
  }
}

void TakActivity::afterMove() {
  if (game.over) {
    showScreen(tak::Screen::Result);
    return;
  }
  if (computersTurn()) {
    thinking = true;
    thinkAt = static_cast<uint32_t>(millis()) + kThinkDelayMs;
  }
  requestUpdate();
}

void TakActivity::playComputer() {
  thinking = false;
  if (!computersTurn()) return;
  const tak::Move move = tak::bestMove(game, gScratch, tak::kMaxMoves, rngState, level);
  const bool played = tak::makeMove(game, move);
  if (!played) LOG_ERR("TAK", "The computer chose a move the rules refused");
  turn.kind = tak::usableKind(game, turn.kind);
  afterMove();
}

// A tap on the board resolves against the layout that drew it, and what it
// means depends on the game, so the meaning (below) covers all of it.
void TakActivity::routeBoardTap(const int x, const int y) {
  if (!surfaceRevealed()) return;
  if (view != tak::Screen::Board || thinking || game.over || computersTurn()) return;
  int cell = 0;
  if (!ui::cellAt(layout, x, y, cell)) return;
  const tak::Outcome outcome = tak::tapCell(game, turn, cell);
  if (outcome.tap == tak::Tap::Ignore) return;
  if (outcome.played) {
    afterMove();
    return;
  }
  requestUpdate();
}

// Everything that changes what a tap on the board does: the screen, the move
// number, what is in hand and how far along it is, and which piece an empty
// square would take. Leaving any out lets a tap queued against the board as it
// WAS act on the board as it has become. See Activity::surfaceMeaning.
uint32_t TakActivity::surfaceMeaning() const {
  uint32_t meaning = paintclock::mixMeaning(paintclock::kMeaningSeed, static_cast<uint32_t>(view));
  meaning = paintclock::mixMeaning(meaning, static_cast<uint32_t>(helpPage));
  meaning = paintclock::mixMeaning(meaning, game.plyNumber);
  meaning = paintclock::mixMeaning(meaning, static_cast<uint32_t>(turn.from + 1));
  meaning = paintclock::mixMeaning(meaning, turn.carry);
  meaning = paintclock::mixMeaning(meaning, turn.steps);
  meaning = paintclock::mixMeaning(meaning, static_cast<uint32_t>(tak::dropped(turn)));
  meaning = paintclock::mixMeaning(meaning, static_cast<uint32_t>(turn.kind));
  return paintclock::mixMeaning(meaning, thinking ? 1u : 0u);
}

void TakActivity::loop() {
  if (thinking && static_cast<int32_t>(static_cast<uint32_t>(millis()) - thinkAt) >= 0) {
    playComputer();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (tak::leavesApp(view)) {
      shelf::leave(renderer, mappedInput);  // no app names its own destination
    } else {
      showScreen(tak::back(view));
    }
    return;
  }

  fui::InputSnapshot input;
  int tapX = 0;
  int tapY = 0;
  if (mappedInput.wasScreenTapped(tapX, tapY)) {
    input.touchReleased = true;
    input.touchX = static_cast<int16_t>(tapX);
    input.touchY = static_cast<int16_t>(tapY);
  }
  if (!input.touchReleased || !interactionsReady) return;

  const fui::ActionEvent action = interactions.route(input);
  switch (action.action) {
    case ui::ActionMenuRow:
      if (view != tak::Screen::Menu) break;
      if (action.value == static_cast<int>(ui::MenuRow::HowTo)) {
        helpPage = 0;
        showScreen(tak::Screen::HowTo);
      } else {
        openSetup(action.value == static_cast<int>(ui::MenuRow::TwoPlayers) ? tak::Opponent::Human
                                                                            : tak::Opponent::Computer);
      }
      break;
    case ui::ActionHowToNext:
      if (view != tak::Screen::HowTo || !surfaceRevealed()) break;
      if (helpPage + 1 < ui::howToPages()) {
        ++helpPage;
        requestUpdate();
      } else {
        showScreen(tak::Screen::Menu);
      }
      break;
    case ui::ActionSize:
      if (view != tak::Screen::Setup || !tak::validSize(action.value)) break;
      boardSize = action.value;
      requestUpdate();
      break;
    case ui::ActionSeat:
      if (view != tak::Screen::Setup) break;
      player = action.value == 1 ? tak::Color::Black : tak::Color::White;
      requestUpdate();
      break;
    case ui::ActionLevel:
      if (view != tak::Screen::Setup || action.value < 0 || action.value > 2) break;
      level = static_cast<tak::Level>(action.value);
      requestUpdate();
      break;
    case ui::ActionStart:
      if (view == tak::Screen::Setup) startGame();
      break;
    case ui::ActionBoard:
      routeBoardTap(tapX, tapY);
      break;
    case ui::ActionKind:
      if (view != tak::Screen::Board || thinking || action.value < 0 || action.value > 2) break;
      if (tak::selectKind(game, turn, kKinds[action.value])) requestUpdate();
      break;
    case ui::ActionPutBack:
      if (view != tak::Screen::Board || !tak::holding(turn)) break;
      tak::clearHand(turn);
      requestUpdate();
      break;
    case ui::ActionAgain:
      if (view == tak::Screen::Result) startGame();
      break;
    case ui::ActionDone:
      if (view == tak::Screen::Result) showScreen(tak::Screen::Menu);
      break;
    default:
      break;
  }
}

void TakActivity::render(RenderLock&&) {
  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer, toybox::toyboxFaces());
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions);
  toybox::Screen screen(frame);

  switch (view) {
    case tak::Screen::Board: {
      ui::BoardModel model;
      model.game = &game;
      model.turn = &turn;
      model.opponent = opponent;
      model.player = player;
      model.thinking = thinking;
      // The same layout the builder will draw from, kept for the next tap.
      layout = ui::layoutFor(target.deviceContext(), game.size);
      ui::buildBoard(screen, model);
      if (++paintsSinceFlash >= kFlashEvery) {
        flashOnNextPaint = true;
        paintsSinceFlash = 0;
      }
      break;
    }
    case tak::Screen::Result: {
      ui::ResultModel model;
      model.game = &game;
      model.opponent = opponent;
      model.player = player;
      ui::buildResult(screen, model);
      break;
    }
    case tak::Screen::Setup: {
      ui::SetupModel model;
      model.vsComputer = opponent == tak::Opponent::Computer;
      model.size = boardSize;
      model.player = player;
      model.level = level;
      ui::buildSetup(screen, model);
      break;
    }
    case tak::Screen::HowTo: {
      ui::HowToModel model;
      model.page = helpPage;
      ui::buildHowTo(screen, model);
      break;
    }
    case tak::Screen::Menu:
    default: {
      ui::MenuModel model;
      model.selected = menuSelected;
      ui::buildMenu(screen, model);
      break;
    }
  }

  interactionsReady = true;
  toybox::reportOverflow(interactions, "Tak");

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(flashOnNextPaint ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
  flashOnNextPaint = false;
}

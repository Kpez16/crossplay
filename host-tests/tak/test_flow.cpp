// The tap-to-move flow, checked without a panel.
//
// The point of these tests is that a person with one finger can say anything the
// engine can play, and nothing it cannot: every slide the engine accepts must be
// reachable by taps, and a tap sequence must never leave a hand that has nowhere
// to go.

#include <cstdint>
#include <cstdio>

#include "TakCore.h"
#include "TakFlow.h"

using namespace tak;

static int checks = 0;
static int failures = 0;

#define CHECK(cond)                                               \
  do {                                                            \
    ++checks;                                                     \
    if (!(cond)) {                                                \
      ++failures;                                                 \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                             \
  } while (0)

namespace {

void setStack(Game& game, const int col, const int row, const char* stones,
              const PieceKind top = PieceKind::Flat) {
  Cell& cell = game.board[at(col, row)];
  cell = Cell{};
  int n = 0;
  uint64_t bits = 0;
  for (; stones[n] != '\0'; ++n) {
    if (stones[n] == 'B') bits |= (1ULL << n);
  }
  cell.height = static_cast<uint8_t>(n);
  cell.colors = bits;
  cell.topKind = top;
}

Game midgame() {
  Game game{};
  game.plyNumber = 2;
  return game;
}

void testAnEmptySquarePlacesTheChosenPiece() {
  Game game = midgame();
  Turn turn;
  CHECK(tapMeaning(game, turn, at(2, 2)) == Tap::Place);
  turn.kind = PieceKind::Wall;
  const Outcome out = tapCell(game, turn, at(2, 2));
  CHECK(out.played);
  CHECK(game.board[at(2, 2)].topKind == PieceKind::Wall);
  CHECK(game.toMove == Color::Black);
}

void testTheOpeningOffersPlacementsOnly() {
  Game game{};
  Turn turn;
  CHECK(!kindAvailable(game, PieceKind::Wall));
  CHECK(!kindAvailable(game, PieceKind::Cap));
  CHECK(kindAvailable(game, PieceKind::Flat));
  CHECK(!selectKind(game, turn, PieceKind::Cap));
  CHECK(tapCell(game, turn, at(0, 0)).played);
  // Black's first turn may not lift White's stone.
  CHECK(tapMeaning(game, turn, at(0, 0)) == Tap::Ignore);
}

void testASingleStoneSlidesInOneStepAndPlaysAtOnce() {
  Game game = midgame();
  setStack(game, 1, 1, "W");
  Turn turn;
  CHECK(tapMeaning(game, turn, at(1, 1)) == Tap::Lift);
  CHECK(!tapCell(game, turn, at(1, 1)).played);
  CHECK(holding(turn) && inHand(turn) == 1);
  CHECK(tapMeaning(game, turn, at(2, 1)) == Tap::Step);
  const Outcome out = tapCell(game, turn, at(2, 1));
  CHECK(out.played);
  CHECK(!holding(turn));
  CHECK(game.board[at(1, 1)].height == 0);
  CHECK(game.board[at(2, 1)].height == 1);
}

void testOnlyYourOwnTopStonesLift() {
  Game game = midgame();
  setStack(game, 0, 0, "WB");  // black on top: not White's
  setStack(game, 1, 0, "BW");
  Turn turn;
  CHECK(tapMeaning(game, turn, at(0, 0)) == Tap::Ignore);
  CHECK(tapMeaning(game, turn, at(1, 0)) == Tap::Lift);
}

void testASlideIsDealtOutAcrossSeveralTaps() {
  Game game = midgame();
  setStack(game, 0, 2, "WWW");
  Turn turn;
  tapCell(game, turn, at(0, 2));
  CHECK(tapMeaning(game, turn, at(0, 2)) == Tap::LiftMore);
  tapCell(game, turn, at(0, 2));
  tapCell(game, turn, at(0, 2));
  CHECK(turn.carry == 3);
  CHECK(tapMeaning(game, turn, at(0, 2)) == Tap::Ignore);  // nothing left to lift
  CHECK(!tapCell(game, turn, at(1, 2)).played);            // first stone dropped
  CHECK(inHand(turn) == 2);
  CHECK(tapMeaning(game, turn, at(1, 2)) == Tap::Add);
  CHECK(tapMeaning(game, turn, at(2, 2)) == Tap::Step);
  CHECK(tapMeaning(game, turn, at(0, 1)) == Tap::Ignore);  // direction is fixed
  CHECK(!tapCell(game, turn, at(2, 2)).played);
  CHECK(tapCell(game, turn, at(3, 2)).played);
  CHECK(game.board[at(1, 2)].height == 1);
  CHECK(game.board[at(2, 2)].height == 1);
  CHECK(game.board[at(3, 2)].height == 1);
  CHECK(game.board[at(0, 2)].height == 0);
}

void testAddingToTheSameSquareKeepsTheHandHere() {
  Game game = midgame();
  setStack(game, 0, 0, "WW");
  Turn turn;
  tapCell(game, turn, at(0, 0));
  tapCell(game, turn, at(0, 0));
  tapCell(game, turn, at(1, 0));
  CHECK(tapMeaning(game, turn, at(1, 0)) == Tap::Add);
  CHECK(tapCell(game, turn, at(1, 0)).played);
  CHECK(game.board[at(1, 0)].height == 2);
}

void testAWallStopsTheHandButACapstoneFlattensIt() {
  Game game = midgame();
  setStack(game, 0, 0, "WW");
  setStack(game, 1, 0, "B", PieceKind::Wall);
  Turn turn;
  tapCell(game, turn, at(0, 0));
  CHECK(tapMeaning(game, turn, at(1, 0)) == Tap::Ignore);
  CHECK(tapMeaning(game, turn, at(0, 1)) == Tap::Step);

  Game game2 = midgame();
  setStack(game2, 0, 0, "W", PieceKind::Cap);
  setStack(game2, 1, 0, "B", PieceKind::Wall);
  Turn turn2;
  tapCell(game2, turn2, at(0, 0));
  CHECK(tapMeaning(game2, turn2, at(1, 0)) == Tap::Step);
  CHECK(tapCell(game2, turn2, at(1, 0)).played);
  CHECK(game2.board[at(1, 0)].topKind == PieceKind::Cap);
  CHECK(game2.board[at(1, 0)].height == 2);
}

void testAStackWithNowhereToGoIsNotOffered() {
  Game game = midgame();
  setStack(game, 0, 0, "W");
  setStack(game, 1, 0, "B", PieceKind::Wall);
  setStack(game, 0, 1, "B", PieceKind::Cap);
  Turn turn;
  CHECK(!canLift(game, at(0, 0)));
  CHECK(tapMeaning(game, turn, at(0, 0)) == Tap::Ignore);
}

void testLiftingAnotherStackReplacesTheHand() {
  Game game = midgame();
  setStack(game, 0, 0, "W");
  setStack(game, 4, 4, "W");
  Turn turn;
  tapCell(game, turn, at(0, 0));
  // A second own stack is not adjacent, so it is not a step; the hand stays.
  CHECK(tapMeaning(game, turn, at(4, 4)) == Tap::Ignore);
  clearHand(turn);
  CHECK(!holding(turn));
  CHECK(tapMeaning(game, turn, at(4, 4)) == Tap::Lift);
}

void testEveryEngineSlideIsReachableByTaps() {
  // A busy position; for each legal slide, tap it out and compare the boards.
  Game base = midgame();
  setStack(base, 2, 2, "BWWBW");
  setStack(base, 1, 2, "B", PieceKind::Wall);
  setStack(base, 3, 2, "B");
  setStack(base, 2, 1, "W", PieceKind::Cap);
  int checkedSlides = 0;
  for (int carry = 1; carry <= 5; ++carry) {
    eachLegalSlide(base, at(2, 2), carry, [&](const Slide& wanted) {
      Game viaEngine = base;
      CHECK(slide(viaEngine, wanted));

      Game viaTaps = base;
      Turn turn;
      tapCell(viaTaps, turn, at(2, 2));
      for (int i = 1; i < carry; ++i) tapCell(viaTaps, turn, at(2, 2));
      int cell = at(2, 2);
      bool played = false;
      for (int sq = 0; sq < wanted.steps; ++sq) {
        cell = cellAfter(base, cell, wanted.direction);
        Outcome out = tapCell(viaTaps, turn, cell);
        for (int extra = 1; extra < wanted.dropCounts[sq]; ++extra) out = tapCell(viaTaps, turn, cell);
        played = out.played;
      }
      CHECK(played);
      bool same = true;
      for (int c = 0; c < kCells; ++c) {
        if (viaTaps.board[c].height != viaEngine.board[c].height ||
            viaTaps.board[c].colors != viaEngine.board[c].colors ||
            viaTaps.board[c].topKind != viaEngine.board[c].topKind) {
          same = false;
        }
      }
      CHECK(same);
      ++checkedSlides;
    });
  }
  CHECK(checkedSlides > 20);
}

void testNoTapSequenceStrandsTheHand() {
  // Walk random taps; whenever the hand is full, some tap must still be offered.
  uint32_t rng = 7;
  const auto roll = [&]() {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
  };
  for (int gameNo = 0; gameNo < 200; ++gameNo) {
    Game game{};
    Turn turn;
    for (int tapNo = 0; tapNo < 300 && !game.over; ++tapNo) {
      if (holding(turn)) CHECK(tapTargets(game, turn) != 0);
      const uint32_t targets = tapTargets(game, turn);
      if (targets == 0) {
        CHECK(!holding(turn));
        break;
      }
      int cell;
      do {
        cell = static_cast<int>(roll() % kCells);
      } while (!((targets >> cell) & 1u));
      if (!holding(turn) && roll() % 5 == 0) {
        const PieceKind kinds[3] = {PieceKind::Flat, PieceKind::Wall, PieceKind::Cap};
        selectKind(game, turn, kinds[roll() % 3]);
      }
      tapCell(game, turn, cell);
    }
  }
}

void testTheSelectorFollowsTheReserve() {
  Game game = midgame();
  Turn turn;
  game.reserve[0].capstones = 0;
  CHECK(!kindAvailable(game, PieceKind::Cap));
  turn.kind = PieceKind::Cap;
  CHECK(usableKind(game, turn.kind) == PieceKind::Flat);
  game.reserve[0].flats = 0;
  CHECK(!kindAvailable(game, PieceKind::Flat));
}

void testTheVerdictReadsFromTheHoldersSeat() {
  Game game{};
  game.over = true;
  game.winner = Color::White;
  CHECK(verdictFor(game, Opponent::Computer) == Verdict::YouWon);
  CHECK(verdictFor(game, Opponent::Human) == Verdict::WhiteWon);
  game.winner = Color::Black;
  CHECK(verdictFor(game, Opponent::Computer) == Verdict::YouLost);
  CHECK(verdictFor(game, Opponent::Human) == Verdict::BlackWon);
  game.draw = true;
  CHECK(verdictFor(game, Opponent::Computer) == Verdict::Draw);
}

void testBackLeavesOnlyFromTheMenu() {
  CHECK(leavesApp(Screen::Menu));
  CHECK(!leavesApp(Screen::Board));
  CHECK(!leavesApp(Screen::Result));
  CHECK(back(Screen::Board) == Screen::Menu);
}

void testTheSmallBoardOffersOnlyItsOwnSquares() {
  Game game;
  reset(game, kSmallSize);
  game.plyNumber = 2;
  Turn turn;
  CHECK(tapMeaning(game, turn, at(1, 1)) == Tap::Place);
  CHECK(tapMeaning(game, turn, at(3, 1)) == Tap::Ignore);
  CHECK(tapMeaning(game, turn, at(1, 3)) == Tap::Ignore);
  CHECK(tapMeaning(game, turn, at(4, 4)) == Tap::Ignore);
  const uint32_t targets = tapTargets(game, turn);
  for (int cell = 0; cell < kCells; ++cell) {
    const bool wanted = colOf(cell) < 3 && rowOf(cell) < 3;
    CHECK((((targets >> cell) & 1u) != 0) == wanted);
  }
  // No capstone exists on this board, so the chooser has nothing to offer.
  CHECK(!kindAvailable(game, PieceKind::Cap));
  CHECK(kindAvailable(game, PieceKind::Wall));
}

void testASmallBoardStackLiftsNoMoreThanItsWidth() {
  Game game;
  reset(game, kSmallSize);
  game.plyNumber = 2;
  setStack(game, 0, 0, "WWWWW");
  Turn turn;
  tapCell(game, turn, at(0, 0));
  tapCell(game, turn, at(0, 0));
  tapCell(game, turn, at(0, 0));
  CHECK(turn.carry == 3);
  CHECK(tapMeaning(game, turn, at(0, 0)) == Tap::Ignore);
  // The edge is the board's, not the grid's: there is nowhere to the right of
  // column 2, but a three-stone hand can still run down the column.
  CHECK(tapMeaning(game, turn, at(1, 0)) == Tap::Step);
  CHECK(tapMeaning(game, turn, at(3, 0)) == Tap::Ignore);
}

void testEverySmallBoardSlideIsReachableByTaps() {
  Game base;
  reset(base, kSmallSize);
  base.plyNumber = 2;
  setStack(base, 1, 1, "BBWW");
  setStack(base, 0, 1, "B", PieceKind::Wall);
  int checkedSlides = 0;
  for (int carry = 1; carry <= 3; ++carry) {
    eachLegalSlide(base, at(1, 1), carry, [&](const Slide& wanted) {
      Game viaEngine = base;
      CHECK(slide(viaEngine, wanted));
      Game viaTaps = base;
      Turn turn;
      tapCell(viaTaps, turn, at(1, 1));
      for (int i = 1; i < carry; ++i) tapCell(viaTaps, turn, at(1, 1));
      int cell = at(1, 1);
      bool played = false;
      for (int sq = 0; sq < wanted.steps; ++sq) {
        cell = cellAfter(base, cell, wanted.direction);
        Outcome out = tapCell(viaTaps, turn, cell);
        for (int extra = 1; extra < wanted.dropCounts[sq]; ++extra) out = tapCell(viaTaps, turn, cell);
        played = out.played;
      }
      CHECK(played);
      bool same = true;
      for (int c = 0; c < kCells; ++c) {
        if (viaTaps.board[c].height != viaEngine.board[c].height ||
            viaTaps.board[c].colors != viaEngine.board[c].colors) {
          same = false;
        }
      }
      CHECK(same);
      ++checkedSlides;
    });
  }
  CHECK(checkedSlides > 8);
}

void testYouWinOrLoseByTheColourYouPlayed() {
  Game game;
  game.over = true;
  game.winner = Color::Black;
  CHECK(verdictFor(game, Opponent::Computer, Color::Black) == Verdict::YouWon);
  CHECK(verdictFor(game, Opponent::Computer, Color::White) == Verdict::YouLost);
  CHECK(verdictFor(game, Opponent::Computer) == Verdict::YouLost);
}

}  // namespace

int main() {
  testAnEmptySquarePlacesTheChosenPiece();
  testTheOpeningOffersPlacementsOnly();
  testASingleStoneSlidesInOneStepAndPlaysAtOnce();
  testOnlyYourOwnTopStonesLift();
  testASlideIsDealtOutAcrossSeveralTaps();
  testAddingToTheSameSquareKeepsTheHandHere();
  testAWallStopsTheHandButACapstoneFlattensIt();
  testAStackWithNowhereToGoIsNotOffered();
  testLiftingAnotherStackReplacesTheHand();
  testEveryEngineSlideIsReachableByTaps();
  testNoTapSequenceStrandsTheHand();
  testTheSelectorFollowsTheReserve();
  testTheVerdictReadsFromTheHoldersSeat();
  testBackLeavesOnlyFromTheMenu();
  testTheSmallBoardOffersOnlyItsOwnSquares();
  testASmallBoardStackLiftsNoMoreThanItsWidth();
  testEverySmallBoardSlideIsReachableByTaps();
  testYouWinOrLoseByTheColourYouPlayed();
  std::printf("%d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}

// The Tak rulebook and its opponent, checked without a panel.
//
// Stacks are described bottom to top: "BWW" is a black stone under two white
// ones. Most tests build a position directly with setStack() rather than
// playing to it, so a failure names the one rule it is about.
//
// The opening swap has the rule most worth holding to account: each player's
// first turn must PLACE an opponent-colored flat. A slide is not a placement,
// and on ply 1 the stone White just put down is Black's, so a core that only
// asks "is the top stone the mover's?" would let Black slide it instead.

#include <cstdint>
#include <cstdio>
#include <initializer_list>

#include "TakBrain.h"
#include "TakCore.h"

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

// Static, as the Activity must declare it: this is a sizeable buffer.
Move gMoves[kMaxMoves];

uint32_t rng = 20261009u;
uint32_t rollDie() {
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return rng;
}

int idx(const Color color) { return static_cast<int>(color); }

// Bottom to top; 'W' white, 'B' black. The caller says what the top is.
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

// The opening swap is over and White is to move.
Game midgame() {
  Game game{};
  game.plyNumber = 2;
  return game;
}

Slide mkSlide(const int col, const int row, const Direction direction, const int pickUp,
              std::initializer_list<int> drops) {
  Slide s;
  s.from = at(col, row);
  s.direction = direction;
  s.pickUp = static_cast<uint8_t>(pickUp);
  int i = 0;
  for (const int v : drops) {
    if (i < kCarryLimit) s.dropCounts[i] = static_cast<uint8_t>(v);
    ++i;
  }
  s.steps = static_cast<uint8_t>(i);
  return s;
}

int stonesOf(const Game& game, const Color color) {
  int total = game.reserve[idx(color)].flats + game.reserve[idx(color)].capstones;
  for (const Cell& cell : game.board) {
    for (int level = 0; level < cell.height; ++level) {
      if (colorAt(cell, level) == color) ++total;
    }
  }
  return total;
}

bool applyMove(Game& game, const Move& move) {
  return move.isPlacement ? place(game, move.placeIndex, move.placeKind) : slide(game, move.slideMove);
}

bool sameMove(const Move& a, const Move& b) {
  if (a.isPlacement != b.isPlacement) return false;
  if (a.isPlacement) return a.placeIndex == b.placeIndex && a.placeKind == b.placeKind;
  const Slide& x = a.slideMove;
  const Slide& y = b.slideMove;
  if (x.from != y.from || x.direction != y.direction || x.pickUp != y.pickUp || x.steps != y.steps) return false;
  for (int i = 0; i < x.steps; ++i) {
    if (x.dropCounts[i] != y.dropCounts[i]) return false;
  }
  return true;
}

Move randomMove(const Game& game) {
  const int count = generateMoves(game, gMoves, kMaxMoves);
  CHECK(count > 0);
  if (count <= 0) return gMoves[0];
  return gMoves[rollDie() % static_cast<uint32_t>(count)];
}

// ---------------------------------------------------------------- setup ----

void testANewGameIsEmptyAndFullyStocked() {
  Game game{};
  reset(game);
  CHECK(game.toMove == Color::White);
  CHECK(game.plyNumber == 0);
  CHECK(!game.over);
  for (const Cell& cell : game.board) CHECK(cell.height == 0);
  for (int c = 0; c < 2; ++c) {
    CHECK(game.reserve[c].flats == kFlatsPerPlayer);
    CHECK(game.reserve[c].capstones == kCapstonesPerPlayer);
  }
  CHECK(kFlatsPerPlayer == 21 && kCapstonesPerPlayer == 1);
  CHECK(kMaxStackHeight <= 64);  // the colors field is a uint64_t
}

// -------------------------------------------------------- opening swap ----

void testEachPlayersFirstStoneIsAnOpponentFlat() {
  Game game{};
  CHECK(colorToPlace(game) == Color::Black);
  CHECK(!canPlace(game, at(2, 2), PieceKind::Wall));
  CHECK(!canPlace(game, at(2, 2), PieceKind::Cap));
  CHECK(canPlace(game, at(2, 2), PieceKind::Flat));

  CHECK(place(game, at(2, 2), PieceKind::Flat));
  CHECK(colorAt(game.board[at(2, 2)], 0) == Color::Black);
  CHECK(game.reserve[idx(Color::Black)].flats == kFlatsPerPlayer - 1);
  CHECK(game.reserve[idx(Color::White)].flats == kFlatsPerPlayer);
  CHECK(game.toMove == Color::Black);
  CHECK(colorToPlace(game) == Color::White);
  CHECK(!canPlace(game, at(0, 0), PieceKind::Wall));
  CHECK(!canPlace(game, at(0, 0), PieceKind::Cap));

  CHECK(place(game, at(0, 0), PieceKind::Flat));
  CHECK(colorAt(game.board[at(0, 0)], 0) == Color::White);
  CHECK(game.reserve[idx(Color::White)].flats == kFlatsPerPlayer - 1);
  CHECK(game.plyNumber == 2);
  // From ply 2 on, a player places their OWN color and may use any piece.
  CHECK(game.toMove == Color::White);
  CHECK(colorToPlace(game) == Color::White);
  CHECK(canPlace(game, at(1, 0), PieceKind::Wall));
  CHECK(canPlace(game, at(1, 0), PieceKind::Cap));
}

void testNobodyMayStartByMovingAStone() {
  Game game{};
  CHECK(place(game, at(2, 2), PieceKind::Flat));  // a BLACK stone; Black is to move
  CHECK(game.toMove == Color::Black);
  const Slide sneaky = mkSlide(2, 2, Direction::Up, 1, {1});
  CHECK(!canSlide(game, sneaky));
  CHECK(!slide(game, sneaky));
  CHECK(game.board[at(2, 2)].height == 1);  // and the refusal changed nothing
  CHECK(game.plyNumber == 1);

  // The generator must agree, or the opponent would play the illegal move.
  const int count = generateMoves(game, gMoves, kMaxMoves);
  CHECK(count == kCells - 1);
  for (int i = 0; i < count; ++i) CHECK(gMoves[i].isPlacement);
}

// ----------------------------------------------------------- placement ----

void testPlacementTargetsOnlyEmptyOnBoardSquares() {
  Game game = midgame();
  CHECK(canPlace(game, 0, PieceKind::Flat));
  CHECK(canPlace(game, kCells - 1, PieceKind::Flat));
  CHECK(!canPlace(game, -1, PieceKind::Flat));
  CHECK(!canPlace(game, kCells, PieceKind::Flat));

  CHECK(place(game, at(1, 1), PieceKind::Flat));
  CHECK(!canPlace(game, at(1, 1), PieceKind::Flat));  // occupied now, by anyone
  CHECK(!place(game, at(1, 1), PieceKind::Wall));
  CHECK(game.plyNumber == 3);  // the refusal did not spend a ply
}

void testReservesLimitWhatCanBePlaced() {
  Game game = midgame();
  game.reserve[idx(Color::White)].flats = 0;
  CHECK(!canPlace(game, 0, PieceKind::Flat));
  CHECK(!canPlace(game, 0, PieceKind::Wall));  // a wall is a flat stone stood on edge
  CHECK(canPlace(game, 0, PieceKind::Cap));

  game.reserve[idx(Color::White)].capstones = 0;
  CHECK(!canPlace(game, 0, PieceKind::Cap));

  Game fresh = midgame();
  CHECK(place(fresh, at(0, 0), PieceKind::Wall));
  CHECK(fresh.board[at(0, 0)].topKind == PieceKind::Wall);
  CHECK(fresh.reserve[idx(Color::White)].flats == kFlatsPerPlayer - 1);
  CHECK(fresh.reserve[idx(Color::White)].capstones == kCapstonesPerPlayer);

  Game capped = midgame();
  CHECK(place(capped, at(0, 0), PieceKind::Cap));
  CHECK(capped.board[at(0, 0)].topKind == PieceKind::Cap);
  CHECK(capped.reserve[idx(Color::White)].flats == kFlatsPerPlayer);
  CHECK(capped.reserve[idx(Color::White)].capstones == 0);
  capped.toMove = Color::White;  // the turn passed to Black, who still holds theirs
  CHECK(!canPlace(capped, at(1, 0), PieceKind::Cap));  // only one capstone each
}

void testAMoveFlipsTheTurnAndCountsAPly() {
  Game game = midgame();
  CHECK(game.toMove == Color::White);
  CHECK(place(game, at(0, 0), PieceKind::Flat));
  CHECK(game.toMove == Color::Black);
  CHECK(game.plyNumber == 3);
  CHECK(colorAt(game.board[at(0, 0)], 0) == Color::White);
}

// -------------------------------------------------------------- slides ----

void testASlideDropsFromTheBottomOfTheCarriedStackFirst() {
  Game game = midgame();
  setStack(game, 0, 0, "BWW");
  const Slide s = mkSlide(0, 0, Direction::Right, 3, {1, 1, 1});
  CHECK(canSlide(game, s));
  CHECK(slide(game, s));
  CHECK(game.board[at(0, 0)].height == 0);
  CHECK(game.board[at(1, 0)].height == 1);
  CHECK(colorAt(game.board[at(1, 0)], 0) == Color::Black);  // the bottom stone went first
  CHECK(colorAt(game.board[at(2, 0)], 0) == Color::White);
  CHECK(colorAt(game.board[at(3, 0)], 0) == Color::White);  // the top stone went last
  CHECK(game.toMove == Color::Black);
  CHECK(game.plyNumber == 3);
}

void testAPartialPickUpLeavesTheRestBehind() {
  Game game = midgame();
  setStack(game, 0, 0, "BWBW");
  const Slide s = mkSlide(0, 0, Direction::Right, 2, {2});
  CHECK(slide(game, s));
  const Cell& rest = game.board[at(0, 0)];
  CHECK(rest.height == 2);
  CHECK(colorAt(rest, 0) == Color::Black && colorAt(rest, 1) == Color::White);
  CHECK(rest.topKind == PieceKind::Flat);
  const Cell& moved = game.board[at(1, 0)];
  CHECK(moved.height == 2);
  CHECK(colorAt(moved, 0) == Color::Black && colorAt(moved, 1) == Color::White);
}

void testTheCarriedTopKeepsItsKindOnTheFinalSquareOnly() {
  Game game = midgame();
  setStack(game, 0, 0, "BW", PieceKind::Wall);
  CHECK(slide(game, mkSlide(0, 0, Direction::Right, 2, {1, 1})));
  CHECK(game.board[at(1, 0)].topKind == PieceKind::Flat);
  CHECK(game.board[at(2, 0)].topKind == PieceKind::Wall);
  CHECK(colorAt(game.board[at(2, 0)], 0) == Color::White);

  // What is left behind a wall or capstone was buried, so it is flat.
  Game left = midgame();
  setStack(left, 0, 0, "BW", PieceKind::Cap);
  CHECK(slide(left, mkSlide(0, 0, Direction::Right, 1, {1})));
  CHECK(left.board[at(0, 0)].height == 1);
  CHECK(left.board[at(0, 0)].topKind == PieceKind::Flat);
  CHECK(left.board[at(1, 0)].topKind == PieceKind::Cap);
}

void testAStoneMayCoverAnyFlatStack() {
  Game game = midgame();
  setStack(game, 0, 0, "W");
  setStack(game, 1, 0, "B");
  CHECK(slide(game, mkSlide(0, 0, Direction::Right, 1, {1})));
  const Cell& stack = game.board[at(1, 0)];
  CHECK(stack.height == 2);
  CHECK(colorAt(stack, 0) == Color::Black && colorAt(stack, 1) == Color::White);
  CHECK(stack.topKind == PieceKind::Flat);
  CHECK(game.board[at(0, 0)].height == 0);
}

void testIllegalSlidesAreRefusedAndChangeNothing() {
  Game game = midgame();
  setStack(game, 2, 2, "WWW");
  setStack(game, 3, 3, "WB");  // an opponent's stack: its top is black
  setStack(game, 0, 4, "WWWWWW");

  CHECK(!canSlide(game, mkSlide(2, 2, Direction::Right, 0, {1})));
  CHECK(!canSlide(game, mkSlide(2, 2, Direction::Right, 4, {4})));      // more than the stack holds
  CHECK(!canSlide(game, mkSlide(2, 2, Direction::Right, 3, {1, 1})));   // drops do not add up
  CHECK(!canSlide(game, mkSlide(2, 2, Direction::Right, 3, {3, 0})));   // a step must drop a stone
  CHECK(!canSlide(game, mkSlide(2, 2, Direction::Right, 3, {1, 1, 1, 0})));
  CHECK(!canSlide(game, mkSlide(2, 2, Direction::Right, 3, {1, 1, 1, 1, 1, 1})));  // six steps
  CHECK(!canSlide(game, mkSlide(0, 4, Direction::Up, 6, {6})));         // over the carry limit
  CHECK(canSlide(game, mkSlide(0, 4, Direction::Up, 5, {5})));          // right at it
  CHECK(!canSlide(game, mkSlide(3, 3, Direction::Left, 1, {1})));       // not yours to move
  CHECK(!canSlide(game, mkSlide(1, 1, Direction::Left, 1, {1})));       // nothing there
  CHECK(!canSlide(game, mkSlide(4, 2, Direction::Right, 1, {1})));      // off the board
  CHECK(!canSlide(game, mkSlide(0, 0, Direction::Left, 1, {1})));
  CHECK(!canSlide(game, mkSlide(0, 0, Direction::Up, 1, {1})));
  CHECK(!canSlide(game, mkSlide(-1, 0, Direction::Up, 1, {1})));

  Slide outside = mkSlide(2, 2, Direction::Right, 1, {1});
  outside.from = kCells;
  CHECK(!canSlide(game, outside));

  const Game before = game;
  CHECK(!slide(game, mkSlide(2, 2, Direction::Right, 4, {4})));
  CHECK(game.plyNumber == before.plyNumber);
  CHECK(game.toMove == before.toMove);
  CHECK(game.board[at(2, 2)].height == 3);

  game.over = true;
  CHECK(!canSlide(game, mkSlide(2, 2, Direction::Right, 1, {1})));
}

void testWallsBlockAndCapstonesBlockEverything() {
  // A flat stack cannot step onto, or through, a standing stone.
  Game game = midgame();
  setStack(game, 0, 0, "WW");
  setStack(game, 1, 0, "B", PieceKind::Wall);
  CHECK(!canSlide(game, mkSlide(0, 0, Direction::Right, 1, {1})));
  CHECK(!canSlide(game, mkSlide(0, 0, Direction::Right, 2, {1, 1})));

  Game beyond = midgame();
  setStack(beyond, 0, 0, "WW");
  setStack(beyond, 2, 0, "W", PieceKind::Wall);  // even your own wall
  CHECK(!canSlide(beyond, mkSlide(0, 0, Direction::Right, 2, {1, 1})));
  CHECK(canSlide(beyond, mkSlide(0, 0, Direction::Right, 1, {1})));

  // A capstone is never landed on or passed through, by anything.
  Game capped = midgame();
  setStack(capped, 0, 0, "W", PieceKind::Cap);
  setStack(capped, 1, 0, "B", PieceKind::Cap);
  CHECK(!canSlide(capped, mkSlide(0, 0, Direction::Right, 1, {1})));

  Game through = midgame();
  setStack(through, 0, 0, "WW");
  setStack(through, 1, 0, "B", PieceKind::Cap);
  CHECK(!canSlide(through, mkSlide(0, 0, Direction::Right, 2, {1, 1})));
}

void testALoneCapstoneFlattensAWall() {
  Game game = midgame();
  setStack(game, 0, 0, "W", PieceKind::Cap);
  setStack(game, 1, 0, "B", PieceKind::Wall);
  const Slide s = mkSlide(0, 0, Direction::Right, 1, {1});
  CHECK(canSlide(game, s));
  CHECK(slide(game, s));
  const Cell& cell = game.board[at(1, 0)];
  CHECK(cell.height == 2);
  CHECK(colorAt(cell, 0) == Color::Black);  // the wall lies down as a flat, still black
  CHECK(colorAt(cell, 1) == Color::White);
  CHECK(cell.topKind == PieceKind::Cap);
}

void testACapstoneMovesOverFlatsAndEmptySquares() {
  Game game = midgame();
  setStack(game, 0, 0, "W", PieceKind::Cap);
  setStack(game, 1, 0, "B");
  CHECK(slide(game, mkSlide(0, 0, Direction::Right, 1, {1})));
  CHECK(game.board[at(1, 0)].height == 2);
  CHECK(game.board[at(1, 0)].topKind == PieceKind::Cap);
}

// The published rule: a capstone that tops a larger stack may drop by itself
// onto a wall at the END of a move, flattening it. It may not do so with
// company, nor before the last square.
void testACapstoneAtopAStackFlattensAWallAtTheEndOfAMove() {
  Game game = midgame();
  setStack(game, 0, 0, "WW", PieceKind::Cap);
  setStack(game, 2, 0, "B", PieceKind::Wall);
  const Slide s = mkSlide(0, 0, Direction::Right, 2, {1, 1});
  CHECK(canSlide(game, s));
  CHECK(slide(game, s));
  CHECK(game.board[at(1, 0)].height == 1);
  CHECK(game.board[at(1, 0)].topKind == PieceKind::Flat);
  const Cell& flattened = game.board[at(2, 0)];
  CHECK(flattened.height == 2);
  CHECK(colorAt(flattened, 0) == Color::Black);  // the wall, lying down
  CHECK(colorAt(flattened, 1) == Color::White);
  CHECK(flattened.topKind == PieceKind::Cap);

  // Not with company: more than the capstone dropped on the wall.
  Game crowded = midgame();
  setStack(crowded, 0, 0, "WWW", PieceKind::Cap);
  setStack(crowded, 1, 0, "B", PieceKind::Wall);
  CHECK(!canSlide(crowded, mkSlide(0, 0, Direction::Right, 2, {2})));

  // Not before the last square: the wall is in the way mid-move.
  Game early = midgame();
  setStack(early, 0, 0, "WW", PieceKind::Cap);
  setStack(early, 1, 0, "B", PieceKind::Wall);
  CHECK(!canSlide(early, mkSlide(0, 0, Direction::Right, 2, {1, 1})));

  // And a stack topped by anything but the capstone gets no such right.
  Game flat = midgame();
  setStack(flat, 0, 0, "WW");
  setStack(flat, 2, 0, "B", PieceKind::Wall);
  CHECK(!canSlide(flat, mkSlide(0, 0, Direction::Right, 2, {1, 1})));
}

// ---------------------------------------------------------------- roads ----

void testAFilledRowIsARoadAndWinsOnTheMove() {
  Game game = midgame();
  for (int c = 0; c < 4; ++c) setStack(game, c, 2, "W");
  CHECK(!hasRoad(game, Color::White));
  CHECK(place(game, at(4, 2), PieceKind::Flat));
  CHECK(hasRoad(game, Color::White));
  CHECK(game.over);
  CHECK(!game.draw);
  CHECK(game.winner == Color::White);
  CHECK(game.winnerByRoad);

  // Nothing is playable once it is over.
  CHECK(!canPlace(game, at(0, 0), PieceKind::Flat));
  CHECK(!place(game, at(0, 0), PieceKind::Flat));
  CHECK(!canSlide(game, mkSlide(4, 2, Direction::Left, 1, {1})));
}

void testAFilledColumnIsARoadToo() {
  Game game = midgame();
  for (int r = 0; r < 4; ++r) setStack(game, 2, r, "W");
  CHECK(!game.over);
  CHECK(place(game, at(2, 4), PieceKind::Flat));
  CHECK(game.over && game.winner == Color::White && game.winnerByRoad);
}

void testARoadMayBendButNotCutCorners() {
  Game snake = midgame();
  setStack(snake, 0, 2, "W");
  setStack(snake, 1, 2, "W");
  setStack(snake, 1, 3, "W");
  setStack(snake, 2, 3, "W");
  setStack(snake, 3, 3, "W");
  setStack(snake, 3, 2, "W");
  setStack(snake, 4, 2, "W");
  CHECK(hasRoad(snake, Color::White));
  CHECK(!hasRoad(snake, Color::Black));

  Game diagonal = midgame();
  for (int i = 0; i < kSize; ++i) setStack(diagonal, i, i, "W");
  CHECK(!hasRoad(diagonal, Color::White));

  Game short4 = midgame();
  for (int c = 0; c < 4; ++c) setStack(short4, c, 2, "W");
  CHECK(!hasRoad(short4, Color::White));
}

void testWallsBreakARoadAndCapstonesCarryOne() {
  Game game = midgame();
  for (int c = 0; c < kSize; ++c) setStack(game, c, 2, "W");
  CHECK(hasRoad(game, Color::White));

  setStack(game, 2, 2, "W", PieceKind::Wall);
  CHECK(!hasRoad(game, Color::White));

  setStack(game, 2, 2, "W", PieceKind::Cap);
  CHECK(hasRoad(game, Color::White));
}

void testOnlyTheTopOfAStackCountsForARoad() {
  Game game = midgame();
  for (int c = 0; c < kSize; ++c) setStack(game, c, 2, "W");
  setStack(game, 2, 2, "WB");  // a black stone sits on top
  CHECK(!hasRoad(game, Color::White));
  CHECK(!hasRoad(game, Color::Black));

  setStack(game, 2, 2, "BW");  // black buried under white: still white's square
  CHECK(hasRoad(game, Color::White));
}

void testASimultaneousRoadGoesToTheMover() {
  Game a = midgame();
  for (int c = 0; c < kSize; ++c) {
    setStack(a, c, 0, "W");
    setStack(a, c, 4, "B");
  }
  Game b = a;
  a.toMove = Color::Black;  // so White has just moved
  checkGameEnd(a);
  CHECK(a.over && a.winner == Color::White && a.winnerByRoad);

  b.toMove = Color::White;  // so Black has just moved
  checkGameEnd(b);
  CHECK(b.over && b.winner == Color::Black && b.winnerByRoad);
}

// ------------------------------------------------------------ flat wins ----

void fillCheckerboard(Game& game) {
  // No two same-colored squares touch, so neither side has a road.
  for (int row = 0; row < kSize; ++row) {
    for (int col = 0; col < kSize; ++col) setStack(game, col, row, ((col + row) % 2 == 0) ? "W" : "B");
  }
}

void testAFullBoardIsDecidedByFlatCount() {
  Game game = midgame();
  fillCheckerboard(game);  // 13 white, 12 black
  checkGameEnd(game);
  CHECK(game.over);
  CHECK(!game.winnerByRoad);
  CHECK(!game.draw);
  CHECK(game.winner == Color::White);
}

// Published rule: only flat stones on top of stacks count. A wall and a
// capstone are both worth nothing at the count.
void testOnlyFlatsCountForTheFlatWin() {
  Game walled = midgame();
  fillCheckerboard(walled);
  setStack(walled, 0, 0, "W", PieceKind::Wall);  // white: 12 flats and a wall
  setStack(walled, 1, 0, "B", PieceKind::Cap);   // black: 11 flats and a capstone
  checkGameEnd(walled);
  CHECK(walled.over && !walled.draw && !walled.winnerByRoad);
  CHECK(walled.winner == Color::White);

  Game capped = midgame();
  fillCheckerboard(capped);
  setStack(capped, 0, 0, "W", PieceKind::Cap);  // white: 12 flats and a capstone
  checkGameEnd(capped);                           // black: 12 flats
  CHECK(capped.over && capped.draw && !capped.winnerByRoad);
}

void testAnEmptyReserveEndsTheGame() {
  Game game = midgame();
  game.reserve[idx(Color::White)].flats = 1;
  game.reserve[idx(Color::White)].capstones = 0;
  setStack(game, 0, 0, "B");
  setStack(game, 1, 0, "B");
  CHECK(place(game, at(4, 4), PieceKind::Flat));
  CHECK(game.over);
  CHECK(!game.winnerByRoad);
  CHECK(!game.draw);
  CHECK(game.winner == Color::Black);  // two flats against one
}

void testACapstoneInHandIsStillAPiece() {
  Game game = midgame();
  game.reserve[idx(Color::White)].flats = 0;
  game.reserve[idx(Color::White)].capstones = 1;
  setStack(game, 0, 0, "W");
  game.toMove = Color::Black;
  checkGameEnd(game);
  CHECK(!game.over);
}

// ---------------------------------------------------------- invariants ----

void testRandomGamesHoldEveryInvariant() {
  int finished = 0;
  int roadWins = 0;
  int flatWins = 0;
  int draws = 0;
  int widest = 0;
  for (int g = 0; g < 400; ++g) {
    Game game{};
    for (int ply = 0; ply < 700 && !game.over; ++ply) {
      const int count = generateMoves(game, gMoves, kMaxMoves);
      CHECK(count > 0);
      CHECK(count < kMaxMoves);  // reaching the cap would mean moves were dropped
      if (count > widest) widest = count;
      if (count <= 0) break;
      const Move move = gMoves[rollDie() % static_cast<uint32_t>(count)];
      CHECK(applyMove(game, move));

      CHECK(stonesOf(game, Color::White) == kFlatsPerPlayer + kCapstonesPerPlayer);
      CHECK(stonesOf(game, Color::Black) == kFlatsPerPlayer + kCapstonesPerPlayer);
      for (const Cell& cell : game.board) {
        CHECK(cell.height <= kMaxStackHeight);
        if (cell.height == 0) CHECK(cell.colors == 0);
      }
    }
    if (game.over) {
      ++finished;
      if (game.draw) {
        ++draws;
      } else if (game.winnerByRoad) {
        ++roadWins;
        CHECK(hasRoad(game, game.winner));
      } else {
        ++flatWins;
        CHECK(!hasRoad(game, Color::White) && !hasRoad(game, Color::Black));
      }
    }
  }
  CHECK(finished == 400);
  std::printf("random games: %d road, %d flat, %d drawn; widest position %d of %d moves\n", roadWins, flatWins,
              draws, widest, kMaxMoves);
}

// ---------------------------------------------------------------- brain ----

void testMoveGenerationCountsAreExactInKnownPositions() {
  Game opening{};
  CHECK(generateMoves(opening, gMoves, kMaxMoves) == kCells);  // a flat anywhere

  Game empty = midgame();
  CHECK(generateMoves(empty, gMoves, kMaxMoves) == kCells * 3);  // flat, wall, cap anywhere

  Game lone = midgame();
  setStack(lone, 2, 2, "W");
  // 24 empty squares x 3 pieces, plus the four one-step slides of the lone flat.
  CHECK(generateMoves(lone, gMoves, kMaxMoves) == (kCells - 1) * 3 + 4);

  Game corner = midgame();
  setStack(corner, 0, 0, "W");
  CHECK(generateMoves(corner, gMoves, kMaxMoves) == (kCells - 1) * 3 + 2);  // two ways off the board

  // A full buffer is truncated, never overrun.
  Move tiny[5];
  CHECK(generateMoves(empty, tiny, 5) == 5);
}

void testRoadDistanceReadsProgress() {
  Game game = midgame();
  CHECK(roadDistance(game, Color::White) == kSize);
  CHECK(roadDistance(game, Color::Black) == kSize);

  for (int c = 0; c < 4; ++c) setStack(game, c, 2, "W");
  CHECK(roadDistance(game, Color::White) == 1);
  CHECK(roadDistance(game, Color::Black) > 1);  // a white row is in black's way

  setStack(game, 4, 2, "W");
  CHECK(roadDistance(game, Color::White) == 0);
}

void testEvaluationSeesWinsLossesAndDraws() {
  Game game = midgame();
  game.over = true;
  game.winner = Color::White;
  CHECK(evaluate(game, Color::White) == 100000);
  CHECK(evaluate(game, Color::Black) == -100000);
  game.draw = true;
  CHECK(evaluate(game, Color::White) == 0);
  CHECK(evaluate(game, Color::Black) == 0);

  Game ahead = midgame();
  setStack(ahead, 0, 2, "W");
  setStack(ahead, 1, 2, "W");
  CHECK(evaluate(ahead, Color::White) > evaluate(ahead, Color::Black));
}

void testTheOpponentTakesAnImmediateWin() {
  Game game = midgame();
  game.plyNumber = 8;
  for (int c = 0; c < 4; ++c) setStack(game, c, 2, "W");
  setStack(game, 0, 0, "B");
  setStack(game, 3, 4, "B");
  uint32_t seed = 12345u;
  const Move move = bestMove(game, gMoves, kMaxMoves, seed);
  Game after = game;
  CHECK(applyMove(after, move));
  CHECK(after.over);
  CHECK(after.winner == Color::White);
}

void testTheOpponentIsDeterministicForAGivenSeed() {
  Game game = midgame();
  setStack(game, 2, 2, "W");
  setStack(game, 1, 1, "B");
  uint32_t a = 777u;
  uint32_t b = 777u;
  const Move first = bestMove(game, gMoves, kMaxMoves, a);
  const Move second = bestMove(game, gMoves, kMaxMoves, b);
  CHECK(sameMove(first, second));
  CHECK(a == b);  // it consumed the same amount of randomness
}

struct Tally {
  int brainWins = 0;
  int otherWins = 0;
  int draws = 0;
  int unfinished = 0;
};

// The opponent plays one color; `brainOther` decides whether the other side is
// also the opponent, otherwise it plays random legal moves. Every move the
// opponent makes must be legal -- that is the point of running it.
Tally playGames(const int games, const Color brainColor, const bool brainOther) {
  Tally tally;
  for (int g = 0; g < games; ++g) {
    Game game{};
    uint32_t seed = 1000u + static_cast<uint32_t>(g);
    for (int ply = 0; ply < 400 && !game.over; ++ply) {
      Move move;
      if (game.toMove == brainColor || brainOther) {
        move = bestMove(game, gMoves, kMaxMoves, seed);
      } else {
        move = randomMove(game);
      }
      CHECK(applyMove(game, move));
    }
    if (!game.over) {
      ++tally.unfinished;
    } else if (game.draw) {
      ++tally.draws;
    } else if (game.winner == brainColor) {
      ++tally.brainWins;
    } else {
      ++tally.otherWins;
    }
  }
  return tally;
}

void testTheOpponentOnlyEverPlaysALegalMove() {
  const Tally white = playGames(60, Color::White, false);
  const Tally black = playGames(60, Color::Black, false);
  const Tally mirror = playGames(30, Color::White, true);
  CHECK(white.unfinished == 0);
  CHECK(black.unfinished == 0);
  CHECK(mirror.unfinished == 0);
}

void testTheOpponentBeatsARandomMoverConvincingly() {
  const Tally asWhite = playGames(60, Color::White, false);
  const Tally asBlack = playGames(60, Color::Black, false);
  const int wins = asWhite.brainWins + asBlack.brainWins;
  const int losses = asWhite.otherWins + asBlack.otherWins;
  std::printf("opponent vs random: %d won, %d lost, %d drawn of 120\n", wins, losses,
              asWhite.draws + asBlack.draws);
  CHECK(wins >= 100);
  CHECK(wins > losses * 5);
}

}  // namespace

int main() {
  testANewGameIsEmptyAndFullyStocked();
  testEachPlayersFirstStoneIsAnOpponentFlat();
  testNobodyMayStartByMovingAStone();
  testPlacementTargetsOnlyEmptyOnBoardSquares();
  testReservesLimitWhatCanBePlaced();
  testAMoveFlipsTheTurnAndCountsAPly();
  testASlideDropsFromTheBottomOfTheCarriedStackFirst();
  testAPartialPickUpLeavesTheRestBehind();
  testTheCarriedTopKeepsItsKindOnTheFinalSquareOnly();
  testAStoneMayCoverAnyFlatStack();
  testIllegalSlidesAreRefusedAndChangeNothing();
  testWallsBlockAndCapstonesBlockEverything();
  testALoneCapstoneFlattensAWall();
  testACapstoneMovesOverFlatsAndEmptySquares();
  testACapstoneAtopAStackFlattensAWallAtTheEndOfAMove();
  testAFilledRowIsARoadAndWinsOnTheMove();
  testAFilledColumnIsARoadToo();
  testARoadMayBendButNotCutCorners();
  testWallsBreakARoadAndCapstonesCarryOne();
  testOnlyTheTopOfAStackCountsForARoad();
  testASimultaneousRoadGoesToTheMover();
  testAFullBoardIsDecidedByFlatCount();
  testOnlyFlatsCountForTheFlatWin();
  testAnEmptyReserveEndsTheGame();
  testACapstoneInHandIsStillAPiece();
  testRandomGamesHoldEveryInvariant();
  testMoveGenerationCountsAreExactInKnownPositions();
  testRoadDistanceReadsProgress();
  testEvaluationSeesWinsLossesAndDraws();
  testTheOpponentTakesAnImmediateWin();
  testTheOpponentIsDeterministicForAGivenSeed();
  testTheOpponentOnlyEverPlaysALegalMove();
  testTheOpponentBeatsARandomMoverConvincingly();

  std::printf("%d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}

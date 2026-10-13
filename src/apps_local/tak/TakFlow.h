#pragma once

// Tak's navigation and its one piece of input state. Header-only, freestanding.
//
// Tak asks more of a tap than any other board game on the shelf. A placement is
// one tap on an empty square, but a SLIDE is a stack, a count of stones to
// lift, a direction, and how the carried stones are dealt out along the way:
// the engine's `Slide` has four fields and the player has to be able to
// express all of them with a finger.
//
// The way it is done here is the way the online clients do it, because it is
// the one players already know:
//
//   1. Tap one of your stacks to lift its top stone. Tap it again for another
//      (up to the carry limit, which is the board's width).
//   2. Tap a neighbouring square: the first stone is dropped there, and the
//      direction is fixed.
//   3. Keep tapping. The square you are on takes another stone; the next
//      square along the line takes one and the hand moves on.
//   4. When the hand is empty the move is played.
//
// **The flow never re-implements a rule.** Every tap is checked by asking the
// engine whether a complete, legal `Slide` still exists that is consistent with
// everything tapped so far (`eachLegalSlide` + `extends`). So a wall, a
// capstone, the carry limit and the board's edge are all handled by the one
// `canSlide` that also guards the AI and the tests, and a tap that would lead
// nowhere is simply not offered. The hand can never be left holding stones that
// have no legal place to go: whenever a tap is accepted, at least one complete
// legal move remains reachable from it.
//
// This is the same discipline as the other board games here: the screen marks
// what the rules offered, it does not work out for itself what might be legal.

#include <cstdint>

#include "TakCore.h"

namespace tak {

enum class Screen : uint8_t {
  // The top. Back from here leaves the app, and it is the only screen that does.
  Menu,
  // Board size, which colour, how hard.
  Setup,
  HowTo,
  Board,
  Result,
};

constexpr Screen back(Screen) { return Screen::Menu; }

constexpr bool leavesApp(const Screen screen) { return screen == Screen::Menu; }

// Who the black stones belong to. The computer always plays black, so the
// person holding the device moves first, which in Tak is the side that places
// the opponent's first stone.
enum class Opponent : uint8_t { Computer, Human };

constexpr int kNothing = -1;

// A turn in progress. Plain data, so the activity can hold it and the screen
// can draw from it.
struct Turn {
  // What a tap on an empty square places.
  PieceKind kind = PieceKind::Flat;
  // The stack in hand, or kNothing.
  int from = kNothing;
  // Stones lifted from it.
  uint8_t carry = 0;
  // Fixed by the first square entered.
  Direction dir = Direction::Up;
  // Squares entered so far, and what has been dropped on each.
  uint8_t steps = 0;
  uint8_t drops[kCarryLimit] = {0, 0, 0, 0, 0};
};

constexpr bool holding(const Turn& turn) { return turn.from != kNothing; }

constexpr int dropped(const Turn& turn) {
  int sum = 0;
  for (int i = 0; i < turn.steps; ++i) sum += turn.drops[i];
  return sum;
}

// Stones still in the hand.
constexpr int inHand(const Turn& turn) { return turn.carry - dropped(turn); }

inline void clearHand(Turn& turn) {
  turn.from = kNothing;
  turn.carry = 0;
  turn.dir = Direction::Up;
  turn.steps = 0;
  for (uint8_t& drop : turn.drops) drop = 0;
}

// The direction from one square to an ORTHOGONAL neighbour, or false.
inline bool directionTo(const int from, const int to, Direction& out) {
  if (from < 0 || from >= kCells || to < 0 || to >= kCells) return false;
  const int dc = colOf(to) - colOf(from);
  const int dr = rowOf(to) - rowOf(from);
  if (dc == 0 && dr == -1) out = Direction::Up;
  else if (dc == 0 && dr == 1) out = Direction::Down;
  else if (dr == 0 && dc == -1) out = Direction::Left;
  else if (dr == 0 && dc == 1) out = Direction::Right;
  else return false;
  return true;
}

// The square one step from `cell`, or kNothing at the edge.
inline int cellAfter(const Game& game, const int cell, const Direction direction) {
  int col = colOf(cell);
  int row = rowOf(cell);
  step(direction, col, row);
  return onBoard(game, col, row) ? at(col, row) : kNothing;
}

// The square the hand is over: where the next stone would go if it stayed.
inline int pathEnd(const Game& game, const Turn& turn) {
  int cell = turn.from;
  for (int i = 0; i < turn.steps && cell != kNothing; ++i) cell = cellAfter(game, cell, turn.dir);
  return cell;
}

// Most stones the stack on `cell` may give up.
inline int maxCarry(const Game& game, const int cell) {
  if (!playable(game, cell)) return 0;
  const int height = game.board[cell].height;
  return height < carryLimit(game) ? height : carryLimit(game);
}

// Calls `visit` with every LEGAL slide that lifts exactly `carry` stones from
// `from`: each of the four directions, each way of dealing the stones out. A
// way of dealing `carry` stones over `steps` squares (each getting at least
// one) is a choice of which of the carry-1 gaps between stones start a new
// square, so the compositions are the bit patterns of a (carry-1)-bit counter.
// Nothing here knows what makes a slide legal; canSlide() does.
template <typename Visit>
inline void eachLegalSlide(const Game& game, const int from, const int carry, Visit&& visit) {
  if (carry < 1 || carry > carryLimit(game)) return;
  const Direction directions[4] = {Direction::Up, Direction::Down, Direction::Left, Direction::Right};
  for (const Direction direction : directions) {
    for (unsigned mask = 0; mask < (1u << (carry - 1)); ++mask) {
      Slide candidate;
      candidate.from = from;
      candidate.direction = direction;
      candidate.pickUp = static_cast<uint8_t>(carry);
      int run = 1;
      int squares = 0;
      for (int gap = 0; gap < carry - 1; ++gap) {
        if ((mask >> gap) & 1u) {
          candidate.dropCounts[squares++] = static_cast<uint8_t>(run);
          run = 1;
        } else {
          ++run;
        }
      }
      candidate.dropCounts[squares++] = static_cast<uint8_t>(run);
      candidate.steps = static_cast<uint8_t>(squares);
      if (canSlide(game, candidate)) visit(candidate);
    }
  }
}

// Whether `cell` holds a stack the player to move can pick up and move
// somewhere. Derived from the legal slides rather than from "is it mine", so a
// stack hemmed in by walls and capstones is not offered: lifting it would leave
// a hand with nowhere to go.
inline bool canLift(const Game& game, const int cell) {
  if (game.over || game.plyNumber < 2) return false;
  if (!playable(game, cell)) return false;
  const Cell& stack = game.board[cell];
  if (stack.height == 0 || colorAt(stack, stack.height - 1) != game.toMove) return false;
  const int most = maxCarry(game, cell);
  bool any = false;
  for (int carry = 1; carry <= most && !any; ++carry) {
    eachLegalSlide(game, cell, carry, [&](const Slide&) { any = true; });
  }
  return any;
}

// Whether a slide still fits everything tapped so far: same direction, and the
// squares already settled hold exactly what they were given, with the square the
// hand is over allowed to take MORE.
inline bool extends(const Turn& turn, const Slide& slide) {
  if (turn.steps == 0) return true;
  if (slide.direction != turn.dir || slide.steps < turn.steps) return false;
  for (int i = 0; i + 1 < turn.steps; ++i) {
    if (slide.dropCounts[i] != turn.drops[i]) return false;
  }
  return slide.dropCounts[turn.steps - 1] >= turn.drops[turn.steps - 1];
}

// What a tap on a square means right now. One function, so the screen can mark
// the squares it would act on and neither can invent a third rule.
enum class Tap : uint8_t {
  // Nothing happens, AND the panel does not repaint: a refresh that comes back
  // identical is what a bug looks like on e-ink.
  Ignore,
  // An empty square, and the chosen piece goes there.
  Place,
  // Pick up the top stone of one of your stacks.
  Lift,
  // One more stone from the stack already in hand.
  LiftMore,
  // Move on to a new square, dropping one stone there.
  Step,
  // Another stone onto the square the hand is over.
  Add,
};

inline Tap tapMeaning(const Game& game, const Turn& turn, const int cell) {
  if (game.over || !playable(game, cell)) return Tap::Ignore;
  const Cell& square = game.board[cell];

  if (!holding(turn)) {
    if (square.height == 0) return canPlace(game, cell, turn.kind) ? Tap::Place : Tap::Ignore;
    return canLift(game, cell) ? Tap::Lift : Tap::Ignore;
  }

  if (turn.steps == 0) {
    if (cell == turn.from) {
      // One more stone only if a complete legal move still exists with it: a
      // capstone that can flatten a wall alone cannot do it carrying a second.
      if (turn.carry >= maxCarry(game, turn.from)) return Tap::Ignore;
      bool any = false;
      eachLegalSlide(game, turn.from, turn.carry + 1, [&](const Slide&) { any = true; });
      return any ? Tap::LiftMore : Tap::Ignore;
    }
    Direction direction;
    if (!directionTo(turn.from, cell, direction)) return Tap::Ignore;
    bool any = false;
    eachLegalSlide(game, turn.from, turn.carry, [&](const Slide& slide) {
      if (slide.direction == direction) any = true;
    });
    return any ? Tap::Step : Tap::Ignore;
  }

  const int end = pathEnd(game, turn);
  if (cell == end) {
    bool any = false;
    eachLegalSlide(game, turn.from, turn.carry, [&](const Slide& slide) {
      if (extends(turn, slide) && slide.dropCounts[turn.steps - 1] > turn.drops[turn.steps - 1]) any = true;
    });
    return any ? Tap::Add : Tap::Ignore;
  }
  if (end != kNothing && cell == cellAfter(game, end, turn.dir)) {
    bool any = false;
    eachLegalSlide(game, turn.from, turn.carry, [&](const Slide& slide) {
      if (extends(turn, slide) && slide.steps > turn.steps &&
          slide.dropCounts[turn.steps - 1] == turn.drops[turn.steps - 1]) {
        any = true;
      }
    });
    return any ? Tap::Step : Tap::Ignore;
  }
  return Tap::Ignore;
}

// Whether the piece the player is about to place can be placed at all: the
// opening swap is flats only, and a reserve can run out of one kind.
inline bool kindAvailable(const Game& game, const PieceKind kind) {
  if (game.over) return false;
  if (game.plyNumber < 2 && kind != PieceKind::Flat) return false;
  const Reserve& reserve = game.reserve[static_cast<int>(colorToPlace(game))];
  return kind == PieceKind::Cap ? reserve.capstones > 0 : reserve.flats > 0;
}

// The chosen piece, or a flat when that kind is no longer available. Called
// after every move, so a spent capstone does not leave a selector that places
// nothing.
inline PieceKind usableKind(const Game& game, const PieceKind wanted) {
  return kindAvailable(game, wanted) ? wanted : PieceKind::Flat;
}

// Choose what an empty-square tap places. Refused while a hand is full (the
// selector is not shown then) and for a kind that is unavailable.
inline bool selectKind(const Game& game, Turn& turn, const PieceKind kind) {
  if (holding(turn) || turn.kind == kind || !kindAvailable(game, kind)) return false;
  turn.kind = kind;
  return true;
}

struct Outcome {
  Tap tap = Tap::Ignore;
  // A whole move was made on the board by this tap.
  bool played = false;
};

// Act on a tap. The only place a Turn changes and the only place a move is
// handed to the engine, so the board and the hand cannot disagree.
inline Outcome tapCell(Game& game, Turn& turn, const int cell) {
  Outcome outcome;
  outcome.tap = tapMeaning(game, turn, cell);

  const auto finishIfEmpty = [&]() {
    if (inHand(turn) != 0) return;
    Slide slideMove;
    slideMove.from = turn.from;
    slideMove.direction = turn.dir;
    slideMove.pickUp = turn.carry;
    slideMove.steps = turn.steps;
    for (int i = 0; i < turn.steps; ++i) slideMove.dropCounts[i] = turn.drops[i];
    outcome.played = slide(game, slideMove);
    clearHand(turn);
    turn.kind = usableKind(game, turn.kind);
  };

  switch (outcome.tap) {
    case Tap::Ignore:
      break;
    case Tap::Place:
      outcome.played = place(game, cell, turn.kind);
      turn.kind = usableKind(game, turn.kind);
      break;
    case Tap::Lift:
      clearHand(turn);
      turn.from = cell;
      turn.carry = 1;
      break;
    case Tap::LiftMore:
      ++turn.carry;
      break;
    case Tap::Step:
      if (turn.steps == 0) directionTo(turn.from, cell, turn.dir);
      turn.drops[turn.steps] = 1;
      ++turn.steps;
      finishIfEmpty();
      break;
    case Tap::Add:
      ++turn.drops[turn.steps - 1];
      finishIfEmpty();
      break;
  }
  return outcome;
}

// Every square a tap would do something on, as a bitmask. This is how the board
// shows what it will accept; a square outside the mask is a tap that does not
// repaint.
inline uint32_t tapTargets(const Game& game, const Turn& turn) {
  uint32_t mask = 0;
  for (int cell = 0; cell < kCells; ++cell) {
    if (tapMeaning(game, turn, cell) != Tap::Ignore) mask |= 1u << cell;
  }
  return mask;
}

// How a finished game reads from the seat of the person holding the device.
enum class Verdict : uint8_t { YouWon, YouLost, WhiteWon, BlackWon, Draw };

// `player` is the colour the person holding the device plays against the
// computer; two people passing it are told the colour instead.
inline Verdict verdictFor(const Game& game, const Opponent opponent, const Color player = Color::White) {
  if (game.draw) return Verdict::Draw;
  if (opponent == Opponent::Human) return game.winner == Color::White ? Verdict::WhiteWon : Verdict::BlackWon;
  return game.winner == player ? Verdict::YouWon : Verdict::YouLost;
}

}  // namespace tak

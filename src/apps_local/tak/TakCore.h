#pragma once

// Tak: the rules. Freestanding -- no renderer, no Activity, no storage, no heap.
// Same shape as GoCore.h and CheckersCore.h: this struct IS the save format,
// trivially copyable, and every rule lives here so Flow/Screens/Brain never
// reimplement a check the engine already makes.
//
// **5x5, 21 flats + 1 capstone per player.** That is the standard/tournament
// size and piece count (Tak scales the count with the board: 4x4 is 15 flats
// and no capstone, 6x6 is 30 and one, 8x8 is 50 and two). Only 5x5 is
// implemented; a second size would need the same treatment Go gave its board
// size -- a field, not a constant -- if it's ever wanted.
//
// **No bit-packing.** Go packs two bits a point because a 13x13 position has
// to fit a 192-byte link-layer packet. Tak v1 is hotseat-only (see the
// backlog: link play is a possible follow-up, not this pass), so there is no
// packet budget to hit, and packing 25 cells of variable-height stacks would
// cost real clarity for a saving nobody needs yet. If link play happens later,
// repacking this struct is the right moment to revisit that trade -- don't
// pre-pack for a constraint that doesn't exist yet.
//
// **A buried piece is always a flat, in effect.** Only the TOP of a stack can
// ever be a Wall or a Capstone: walls and capstones can only be placed onto an
// empty square (never onto an existing stack), and the one exception --
// a capstone dropping alone onto a wall at the end of a move -- FLATTENS that wall into a flat as
// part of the move, rather than burying it intact. So every level below the
// top, at every point in the game, behaves like a flat: it still carries an
// owner (for when a later slide picks the stack back up), but its "kind" is
// never inspected once it stops being the top. That is why `Cell` stores one
// `topKind` rather than a kind per level -- there is nothing else to store.

#include <cstdint>

namespace tak {

constexpr int kSize = 5;
constexpr int kCells = kSize * kSize;
constexpr int kFlatsPerPlayer = 21;
constexpr int kCapstonesPerPlayer = 1;
// Largest single move: you may pick up at most as many stones as the board is
// wide (the standard "carry limit" rule -- it scales with board size).
constexpr int kCarryLimit = kSize;
// Ceiling on a single stack's height. Every one of the 44 pieces in the game
// landing in one column is not legal in any realistic line of play, but it is
// also not something the rules forbid outright, so size for it rather than
// assert it can't happen -- the same reasoning Go's kMaxSize documents.
constexpr int kMaxStackHeight = 2 * (kFlatsPerPlayer + kCapstonesPerPlayer);

enum class Color : uint8_t { White = 0, Black = 1 };
enum class PieceKind : uint8_t { Flat = 0, Wall = 1, Cap = 2 };

constexpr Color opposite(Color color) {
  return color == Color::White ? Color::Black : Color::White;
}

// One board square. `colors` bit i is the owner of the piece at level i
// (0 = White, 1 = Black); only bits [0, height) are meaningful. `topKind` is
// the kind of the piece at level (height - 1) and is meaningless when
// height == 0.
struct Cell {
  uint8_t height = 0;
  uint64_t colors = 0;
  PieceKind topKind = PieceKind::Flat;
};

inline Color colorAt(const Cell& cell, int level) {
  return ((cell.colors >> level) & 1ULL) ? Color::Black : Color::White;
}

inline bool topCountsForRoad(const Cell& cell, Color color) {
  // Walls block roads; flats and capstones carry them. An empty cell never
  // counts, and a cell whose top belongs to the other color never counts.
  if (cell.height == 0) return false;
  if (cell.topKind == PieceKind::Wall) return false;
  return colorAt(cell, cell.height - 1) == color;
}

struct Reserve {
  uint8_t flats = kFlatsPerPlayer;
  uint8_t capstones = kCapstonesPerPlayer;
};

struct Game {
  Cell board[kCells];
  Reserve reserve[2];  // indexed by Color
  Color toMove = Color::White;
  // 0-based. Plies 0 and 1 are the opening swap: each player's FIRST placement
  // is an opponent-colored flat, so a new game doesn't hand White a
  // first-move advantage from an otherwise-symmetric position.
  uint16_t plyNumber = 0;
  bool over = false;
  Color winner = Color::White;  // meaningless unless over && !draw
  bool winnerByRoad = false;    // false => won (or drew) on flat count
  bool draw = false;            // meaningless unless over
};

constexpr int at(int col, int row) { return row * kSize + col; }
constexpr int colOf(int index) { return index % kSize; }
constexpr int rowOf(int index) { return index / kSize; }
constexpr bool onBoard(int col, int row) {
  return col >= 0 && col < kSize && row >= 0 && row < kSize;
}

inline void reset(Game& game) {
  game = Game{};
}

// The color whose piece is placed THIS turn -- not necessarily game.toMove,
// because of the opening swap (see plyNumber's comment above).
inline Color colorToPlace(const Game& game) {
  return game.plyNumber < 2 ? opposite(game.toMove) : game.toMove;
}

inline bool canPlace(const Game& game, int index, PieceKind kind) {
  if (game.over) return false;
  if (index < 0 || index >= kCells) return false;
  if (game.board[index].height != 0) return false;  // placement targets empty squares only
  if (game.plyNumber < 2 && kind != PieceKind::Flat) return false;  // swap turns are flats only
  const Color color = colorToPlace(game);
  const Reserve& reserve = game.reserve[static_cast<int>(color)];
  if (kind == PieceKind::Cap) return reserve.capstones > 0;
  return reserve.flats > 0;  // Flat and Wall both spend a flat stone
}

// Forward-declared; defined below reset/place/slide so it can see the whole
// board after either kind of move.
inline void checkGameEnd(Game& game);

inline bool place(Game& game, int index, PieceKind kind) {
  if (!canPlace(game, index, kind)) return false;
  const Color color = colorToPlace(game);
  Cell& cell = game.board[index];
  cell.height = 1;
  cell.colors = static_cast<uint64_t>(color);
  cell.topKind = kind;
  Reserve& reserve = game.reserve[static_cast<int>(color)];
  if (kind == PieceKind::Cap) {
    reserve.capstones--;
  } else {
    reserve.flats--;
  }
  game.toMove = opposite(game.toMove);
  game.plyNumber++;
  checkGameEnd(game);
  return true;
}

enum class Direction : uint8_t { Up, Down, Left, Right };

inline void step(Direction direction, int& col, int& row) {
  switch (direction) {
    case Direction::Up: row--; break;
    case Direction::Down: row++; break;
    case Direction::Left: col--; break;
    case Direction::Right: col++; break;
  }
}

// A slide: pick up the top `pickUp` stones of `from`, then walk `direction`
// for `steps` squares, dropping `dropCounts[i]` stones (>=1 each) on the i-th
// square. Stones are handed out from the BOTTOM of the carried group first --
// so the square at steps-1 (the final square) always receives what was
// originally the TOP of the source stack, kind included. That is the only
// square a Wall or Capstone can newly appear on top of; every earlier square
// in the path can only ever receive a (previously-buried, therefore flat)
// stone. See the file header for why that invariant holds.
struct Slide {
  int from = 0;
  Direction direction = Direction::Up;
  uint8_t pickUp = 0;
  uint8_t steps = 0;
  uint8_t dropCounts[kCarryLimit] = {0};
};

inline bool canSlide(const Game& game, const Slide& slide) {
  if (game.over) return false;
  // The opening swap is two PLACEMENTS. On ply 1 the stone White just put down
  // is Black's, so without this Black could slide it instead of placing.
  if (game.plyNumber < 2) return false;
  if (slide.from < 0 || slide.from >= kCells) return false;
  const Cell& src = game.board[slide.from];
  if (src.height == 0) return false;
  if (colorAt(src, src.height - 1) != game.toMove) return false;  // must move your own top piece
  if (slide.pickUp < 1 || slide.pickUp > kCarryLimit || slide.pickUp > src.height) return false;
  if (slide.steps < 1 || slide.steps > kCarryLimit) return false;

  uint32_t sum = 0;
  for (int i = 0; i < slide.steps; i++) {
    if (slide.dropCounts[i] < 1) return false;
    sum += slide.dropCounts[i];
  }
  if (sum != slide.pickUp) return false;

  int col = colOf(slide.from);
  int row = rowOf(slide.from);
  for (int i = 0; i < slide.steps; i++) {
    step(slide.direction, col, row);
    if (!onBoard(col, row)) return false;
    const Cell& dest = game.board[at(col, row)];
    if (dest.height == 0) continue;  // empty square: always fine
    if (dest.topKind == PieceKind::Cap) return false;  // a capstone can never be landed on, or passed through
    if (dest.topKind == PieceKind::Wall) {
      const bool isFinal = (i == slide.steps - 1);
      const bool capstoneOnTop = (src.topKind == PieceKind::Cap);
      // The one exception, per the published rules: a capstone that tops the
      // carried stones may drop BY ITSELF onto a wall as the FINAL square of
      // the move, flattening it. The final square receives the top stone, so
      // a drop of exactly one there is the capstone alone. Anything else --
      // passing through a wall, or landing on one with more than the bare
      // capstone -- is illegal; a wall blocks the line entirely otherwise.
      if (!(isFinal && capstoneOnTop && slide.dropCounts[i] == 1)) return false;
    }
  }
  return true;
}

inline bool slide(Game& game, const Slide& s) {
  if (!canSlide(game, s)) return false;

  Cell& src = game.board[s.from];
  const PieceKind carriedTopKind = src.topKind;
  // Bits 0..pickUp-1 of `carried` are the picked-up levels, bottom to top --
  // bit 0 is dropped first (first square of the path), the last bit is
  // dropped last (final square), matching the rule in the Slide comment above.
  const uint64_t carried = (src.colors >> (src.height - s.pickUp)) & ((1ULL << s.pickUp) - 1);

  src.height -= s.pickUp;
  src.colors = (src.height == 0) ? 0 : (src.colors & ((1ULL << src.height) - 1));
  if (src.height > 0) src.topKind = PieceKind::Flat;  // what remains was always buried, i.e. flat

  int col = colOf(s.from);
  int row = rowOf(s.from);
  int consumed = 0;  // index into `carried`, counting up from the bottom of the carried group
  for (int i = 0; i < s.steps; i++) {
    step(s.direction, col, row);
    Cell& dest = game.board[at(col, row)];
    const bool isFinal = (i == s.steps - 1);

    if (dest.height > 0 && dest.topKind == PieceKind::Wall) {
      dest.topKind = PieceKind::Flat;  // validated by canSlide: this can only be the lone-capstone flatten
    }

    for (int k = 0; k < s.dropCounts[i]; k++) {
      const Color c = ((carried >> consumed) & 1ULL) ? Color::Black : Color::White;
      dest.colors |= (static_cast<uint64_t>(c) << dest.height);
      dest.height++;
      consumed++;
    }
    dest.topKind = isFinal ? carriedTopKind : PieceKind::Flat;
  }

  game.toMove = opposite(game.toMove);
  game.plyNumber++;
  checkGameEnd(game);
  return true;
}

// True if `color` connects any two opposite edges through an unbroken chain
// of orthogonally-adjacent road-carrying squares (see topCountsForRoad).
inline bool hasRoad(const Game& game, Color color) {
  bool visited[kCells];
  for (int i = 0; i < kCells; i++) visited[i] = false;

  auto floodFrom = [&](bool horizontal) -> bool {
    for (int i = 0; i < kCells; i++) visited[i] = false;
    int stack[kCells];
    int sp = 0;
    for (int n = 0; n < kSize; n++) {
      const int idx = horizontal ? at(0, n) : at(n, 0);
      if (!visited[idx] && topCountsForRoad(game.board[idx], color)) {
        visited[idx] = true;
        stack[sp++] = idx;
      }
    }
    while (sp > 0) {
      const int idx = stack[--sp];
      const int reachedEdge = horizontal ? colOf(idx) : rowOf(idx);
      if (reachedEdge == kSize - 1) return true;
      const int c = colOf(idx);
      const int r = rowOf(idx);
      constexpr int dc[4] = {1, -1, 0, 0};
      constexpr int dr[4] = {0, 0, 1, -1};
      for (int d = 0; d < 4; d++) {
        const int nc = c + dc[d];
        const int nr = r + dr[d];
        if (!onBoard(nc, nr)) continue;
        const int nidx = at(nc, nr);
        if (visited[nidx]) continue;
        if (!topCountsForRoad(game.board[nidx], color)) continue;
        visited[nidx] = true;
        stack[sp++] = nidx;
      }
    }
    return false;
  };

  return floodFrom(/*horizontal=*/true) || floodFrom(/*horizontal=*/false);
}

// Call after every place()/slide() -- both already do this internally, so
// callers never need to invoke it directly; it's exposed for tests.
inline void checkGameEnd(Game& game) {
  if (game.over) return;

  // The move that was just made belongs to whoever held toMove before this
  // function's caller flipped it, i.e. the color that is NOT toMove now.
  const Color mover = opposite(game.toMove);
  // If the mover's move completes a road for both players at once (possible
  // when flattening a wall reconnects the opponent too), the mover wins --
  // the standard Tak tie-break for a simultaneous road.
  if (hasRoad(game, mover)) {
    game.over = true;
    game.winner = mover;
    game.winnerByRoad = true;
    return;
  }
  if (hasRoad(game, game.toMove)) {
    game.over = true;
    game.winner = game.toMove;
    game.winnerByRoad = true;
    return;
  }

  const bool reserveEmpty = (game.reserve[0].flats == 0 && game.reserve[0].capstones == 0) ||
                             (game.reserve[1].flats == 0 && game.reserve[1].capstones == 0);
  bool boardFull = true;
  for (const Cell& cell : game.board) {
    if (cell.height == 0) {
      boardFull = false;
      break;
    }
  }
  if (!reserveEmpty && !boardFull) return;

  // Flat count, per the published rules: only FLAT stones on top of stacks
  // count. Walls and capstones do not. Highest count wins, equal counts draw.
  int flatCount[2] = {0, 0};
  for (const Cell& cell : game.board) {
    if (cell.height == 0 || cell.topKind != PieceKind::Flat) continue;
    flatCount[static_cast<int>(colorAt(cell, cell.height - 1))]++;
  }
  game.over = true;
  game.winnerByRoad = false;
  if (flatCount[0] == flatCount[1]) {
    game.draw = true;
  } else {
    game.winner = flatCount[0] > flatCount[1] ? Color::White : Color::Black;
  }
}

}  // namespace tak

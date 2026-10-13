#pragma once

// The built-in opponent. Freestanding, like the rules it plays by -- reads
// TakCore.h only, through place()/slide()/canPlace()/canSlide(), so there is
// nothing here it could "cheat" with even if it wanted to: Tak carries no
// hidden information to cheat about in the first place, unlike a card game.
//
// **Three levels, and what each one is.** EASY plays a random legal move about
// half the time and the evaluation's pick the rest, so it hangs stones and
// misses wins like a beginner. MEDIUM is the plain one-ply search described
// below. HARD adds the one thing one ply cannot see: it takes its best few
// candidates and, for each, plays out every reply the opponent has, so it
// never allows a road it could have stopped and takes a win when one exists.
// That is a two-ply search over a short list rather than over all several
// hundred moves, which is what keeps it inside the frame budget the next
// paragraph is worried about: roughly ten candidates times a few hundred
// replies is a few thousand evaluations, not a few hundred thousand.
//
// **One ply, not two** (for EASY and MEDIUM). CheckersBrain.h searches two ply because a one-ply
// checkers bot hangs pieces constantly -- a free capture is very often a
// trap. Tak's problem is the opposite: it is NOT that one ply plays badly,
// it's that a full ply here is enormously more expensive than a checkers ply.
// A checkers move is "a piece, a direction"; a Tak move is "a stack, a
// direction, how many stones to pick up, how many steps, and how to divide
// the stones across those steps" -- kMaxMoves below is sized in the hundreds
// for a single 5x5 position, not the dozens Checkers deals with. Searching
// a second ply means replaying every one of the opponent's replies to every
// one of those hundreds of candidates, which is the kind of unbounded-time
// operation the render task's watchdog does not forgive. One ply, with an
// evaluation that already accounts for road progress rather than just
// material, is the responsible default until someone profiles two ply on
// real hardware and proves it fits the frame budget. Flagged in the backlog
// as a follow-up, not assumed here.
//
// **Its blind spot, stated plainly rather than discovered by a player:**
// because it only looks one ply deep, it cannot see that a move lets the
// opponent complete a road on THEIR next turn. It will walk into that. A
// two-ply search is the fix, when it's warranted (see above).
//
// **The evaluation is a heuristic, not a solved value.** `roadDistance()`
// answers "how many friendly-favourable squares stand between me and a
// finished road," using a cheap per-square cost (0 = already mine, 1 = empty,
// 3 = occupied by anything else) fed through Dijkstra. It ignores whose turn
// it is, reserve limits, and the opponent's ability to interfere -- it is a
// proxy for "closer is better," not a move-count guarantee. That is the same
// spirit as CheckersBrain's material count: crude on purpose, because a
// heavier evaluation costs search time this design does not have to spend.

#include <cstdint>

#include "TakCore.h"

namespace tak {

// One candidate move, either a placement or a slide. Move generation fills a
// caller-supplied buffer of these -- see kMaxMoves below for why that buffer
// must be static, never a render-task stack local.
struct Move {
  bool isPlacement = false;
  uint8_t placeIndex = 0;
  PieceKind placeKind = PieceKind::Flat;
  Slide slideMove{};
};

// A generous ceiling for one 5x5 position: up to 25 cells x 3 kinds for
// placements, plus, for every stack the mover owns, 4 directions x
// (pickUp from 1..5) x (every way to divide that pickUp across 1..pickUp
// steps) slides. The true worst case is well under this in practice --
// generateMoves() stops filling and returns early if it's ever reached, it
// does not overflow the caller's buffer.
//
// sizeof(Move) is on the order of 20-24 bytes, so a full kMaxMoves buffer is
// a meaningful fraction of CROSSPOINT_RENDER_TASK_STACK (16384 bytes,
// platformio.ini). Declare the buffer `static` (BSS/DRAM) in whatever
// Activity calls bestMove(), never as a local on the render task's stack --
// the same rule CLAUDE.md states for any allocation this size.
constexpr int kMaxMoves = 600;

// (Level is defined in TakCore.h so the screens can name it without this file.)

// How many candidates HARD looks two plies deep on.
constexpr int kHardCandidates = 10;

inline int generatePlacements(const Game& game, Move* out, int capacity) {
  int count = 0;
  const PieceKind kinds[3] = {PieceKind::Flat, PieceKind::Wall, PieceKind::Cap};
  const int kindCount = (game.plyNumber < 2) ? 1 : 3;  // swap plies: flats only
  for (int idx = 0; idx < kCells && count < capacity; idx++) {
    if (!playable(game, idx)) continue;
    for (int k = 0; k < kindCount; k++) {
      if (!canPlace(game, idx, kinds[k])) continue;
      out[count].isPlacement = true;
      out[count].placeIndex = static_cast<uint8_t>(idx);
      out[count].placeKind = kinds[k];
      count++;
      if (count >= capacity) return count;
    }
  }
  return count;
}

// Recursively enumerates every way to split `remaining` stones across the
// steps still to come (`steps - depth` of them, each getting at least one
// stone), filling `counts[depth..steps)` as it goes and testing the
// resulting Slide with canSlide() once a full composition is assembled.
inline void tryDropCompositions(const Game& game, int fromIndex, Direction direction, uint8_t pickUp,
                                 uint8_t steps, uint8_t* counts, uint8_t depth, int remaining, Move* out,
                                 int& count, int capacity) {
  if (count >= capacity) return;
  if (depth == steps) {
    Slide candidate;
    candidate.from = fromIndex;
    candidate.direction = direction;
    candidate.pickUp = pickUp;
    candidate.steps = steps;
    for (int i = 0; i < steps; i++) candidate.dropCounts[i] = counts[i];
    if (canSlide(game, candidate)) {
      out[count].isPlacement = false;
      out[count].slideMove = candidate;
      count++;
    }
    return;
  }
  // Every one of the remaining (steps - depth - 1) steps after this one still
  // needs at least one stone, so this step may take at most what's left once
  // that's reserved for them.
  const int stepsAfterThis = steps - depth - 1;
  const int maxHere = remaining - stepsAfterThis;
  for (int take = 1; take <= maxHere && count < capacity; take++) {
    counts[depth] = static_cast<uint8_t>(take);
    tryDropCompositions(game, fromIndex, direction, pickUp, steps, counts, depth + 1, remaining - take, out,
                         count, capacity);
  }
}

inline int generateSlides(const Game& game, Move* out, int capacity) {
  int count = 0;
  const Direction directions[4] = {Direction::Up, Direction::Down, Direction::Left, Direction::Right};
  for (int idx = 0; idx < kCells && count < capacity; idx++) {
    const Cell& cell = game.board[idx];
    if (cell.height == 0) continue;
    if (colorAt(cell, cell.height - 1) != game.toMove) continue;  // only your own stacks move
    const int limit = carryLimit(game);
    const int maxPickUp = (cell.height < limit) ? cell.height : limit;
    for (int d = 0; d < 4 && count < capacity; d++) {
      for (uint8_t pickUp = 1; pickUp <= maxPickUp && count < capacity; pickUp++) {
        for (uint8_t steps = 1; steps <= pickUp && count < capacity; steps++) {
          uint8_t counts[kCarryLimit];
          tryDropCompositions(game, idx, directions[d], pickUp, steps, counts, 0, pickUp, out, count, capacity);
        }
      }
    }
  }
  return count;
}

inline int generateMoves(const Game& game, Move* out, int capacity) {
  int count = generatePlacements(game, out, capacity);
  count += generateSlides(game, out + count, capacity - count);
  return count;
}

// Shortest-path "how close to a road" estimate for `color`, using a per-cell
// cost (0 = already a road-carrying cell of this color, 1 = empty, 3 =
// anything else -- an opponent-topped stack or a wall) fed through a plain
// O(cells^2) Dijkstra. 25 cells makes the naive form fast enough that a
// bucket queue would only add code, not speed. See the file header for what
// this number does and doesn't mean.
inline int roadDistance(const Game& game, Color color) {
  auto costOf = [&](int index) -> int {
    const Cell& cell = game.board[index];
    if (topCountsForRoad(cell, color)) return 0;
    if (cell.height == 0) return 1;
    return 3;
  };

  auto shortest = [&](bool horizontal) -> int {
    constexpr int kInfinite = 1 << 20;
    int dist[kCells];
    bool settled[kCells];
    for (int i = 0; i < kCells; i++) {
      dist[i] = kInfinite;
      // Squares off a small board are never reached.
      settled[i] = !playable(game, i);
    }
    for (int n = 0; n < game.size; n++) {
      const int idx = horizontal ? at(0, n) : at(n, 0);
      dist[idx] = costOf(idx);
    }
    for (int iter = 0; iter < kCells; iter++) {
      int u = -1;
      int best = kInfinite;
      for (int i = 0; i < kCells; i++) {
        if (!settled[i] && dist[i] < best) {
          best = dist[i];
          u = i;
        }
      }
      if (u < 0) break;
      settled[u] = true;
      const int c = colOf(u);
      const int r = rowOf(u);
      constexpr int dc[4] = {1, -1, 0, 0};
      constexpr int dr[4] = {0, 0, 1, -1};
      for (int d = 0; d < 4; d++) {
        const int nc = c + dc[d];
        const int nr = r + dr[d];
        if (!onBoard(game, nc, nr)) continue;
        const int v = at(nc, nr);
        if (settled[v]) continue;
        const int candidate = dist[u] + costOf(v);
        if (candidate < dist[v]) dist[v] = candidate;
      }
    }
    int best = kInfinite;
    for (int n = 0; n < game.size; n++) {
      const int idx = horizontal ? at(game.size - 1, n) : at(n, game.size - 1);
      if (dist[idx] < best) best = dist[idx];
    }
    return best;
  };

  const int viaRows = shortest(true);
  const int viaCols = shortest(false);
  return viaRows < viaCols ? viaRows : viaCols;
}

// Score of `game` from `forColor`'s point of view. Flat differential matters
// (it decides the game outright when reserves run out or the board fills),
// but road progress matters far more in the middle game, hence the 3x weight
// -- tuned by feel, not measured, same as CheckersBrain's piece values.
inline int evaluate(const Game& game, Color forColor) {
  if (game.over) {
    if (game.draw) return 0;
    return (game.winner == forColor) ? 100000 : -100000;
  }
  const Color other = opposite(forColor);
  int flatDiff = 0;
  for (const Cell& cell : game.board) {
    if (cell.height == 0 || cell.topKind != PieceKind::Flat) continue;  // only flats count, as in the real flat win
    flatDiff += (colorAt(cell, cell.height - 1) == forColor) ? 1 : -1;
  }
  const int myRoad = roadDistance(game, forColor);
  const int theirRoad = roadDistance(game, other);
  return flatDiff * 2 + (theirRoad - myRoad) * 6;
}

// Tiny dependency-free PRNG (xorshift32) used only to break ties between
// equally-scored moves, so the opponent doesn't play the same line every time
// it faces the same position. Not used for anything that needs real entropy.
inline uint32_t nextRandom(uint32_t& state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

// Picks a move for game.toMove: generate every legal move, apply each to a
// scratch copy of the position, score the result, and keep the best --
// reservoir-sampling a random pick among ties so equally good moves aren't
// always played in the same order. `scratch` must hold at least
// scratchCapacity Move entries and must be allocated statically (see
// kMaxMoves).
inline Move bestOnePly(const Game& game, Move* scratch, int scratchCapacity, uint32_t& rngState) {
  const int count = generateMoves(game, scratch, scratchCapacity);
  // Tak never strands the player to move without a legal move: an empty
  // square is always a legal placement target as long as any reserve piece
  // remains, and a player with an empty reserve and a full board would
  // already have ended the game via checkGameEnd's board-full check. Reaching
  // this function with count == 0 means it was called on a position that
  // should already have game.over == true -- a caller bug, not a game state
  // to handle gracefully.
  int bestIndex = 0;
  int bestScore = -(1 << 30);
  int tieCount = 0;
  for (int i = 0; i < count; i++) {
    Game scratchGame = game;
    const bool applied = scratch[i].isPlacement
                              ? place(scratchGame, scratch[i].placeIndex, scratch[i].placeKind)
                              : slide(scratchGame, scratch[i].slideMove);
    if (!applied) continue;  // defensive only -- generateMoves already filtered to legal moves
    const int score = evaluate(scratchGame, game.toMove);
    if (score > bestScore) {
      bestScore = score;
      bestIndex = i;
      tieCount = 1;
    } else if (score == bestScore) {
      tieCount++;
      if ((nextRandom(rngState) % static_cast<uint32_t>(tieCount)) == 0) bestIndex = i;
    }
  }
  return scratch[bestIndex];
}

inline bool makeMove(Game& game, const Move& move) {
  return move.isPlacement ? place(game, move.placeIndex, move.placeKind) : slide(game, move.slideMove);
}

// HARD: the best kHardCandidates moves by one-ply score, each then tried
// against every reply, scored by the worst reply the opponent has.
inline Move bestTwoPly(const Game& game, Move* scratch, int scratchCapacity, uint32_t& rngState) {
  static Move top[kHardCandidates];
  int topScore[kHardCandidates];
  int topCount = 0;

  const int count = generateMoves(game, scratch, scratchCapacity);
  const Color me = game.toMove;
  for (int i = 0; i < count; i++) {
    Game after = game;
    if (!makeMove(after, scratch[i])) continue;
    const int score = evaluate(after, me);
    // Insert into the short list, best first; ties keep the earlier move, and
    // the shuffle below is what varies play.
    int at = topCount < kHardCandidates ? topCount : kHardCandidates;
    if (at == kHardCandidates && score <= topScore[kHardCandidates - 1]) continue;
    if (at == kHardCandidates) at = kHardCandidates - 1;
    while (at > 0 && topScore[at - 1] < score) {
      if (at < kHardCandidates) {
        topScore[at] = topScore[at - 1];
        top[at] = top[at - 1];
      }
      --at;
    }
    topScore[at] = score;
    top[at] = scratch[i];
    if (topCount < kHardCandidates) ++topCount;
  }
  if (topCount == 0) return scratch[0];

  int bestIndex = 0;
  int bestScore = -(1 << 30);
  int tieCount = 0;
  for (int c = 0; c < topCount; c++) {
    Game after = game;
    makeMove(after, top[c]);
    int worst;
    if (after.over) {
      worst = evaluate(after, me);  // a win is +100000 and ends the search at once
    } else {
      worst = 1 << 30;
      const int replies = generateMoves(after, scratch, scratchCapacity);
      for (int r = 0; r < replies; r++) {
        Game reply = after;
        if (!makeMove(reply, scratch[r])) continue;
        const int score = evaluate(reply, me);
        if (score < worst) worst = score;
        if (worst <= -100000) break;  // they have a win; no need to look further
      }
      if (worst == (1 << 30)) worst = evaluate(after, me);
    }
    if (worst > bestScore) {
      bestScore = worst;
      bestIndex = c;
      tieCount = 1;
    } else if (worst == bestScore) {
      tieCount++;
      if ((nextRandom(rngState) % static_cast<uint32_t>(tieCount)) == 0) bestIndex = c;
    }
  }
  return top[bestIndex];
}

// The opponent's move at a level. Same contract as bestOnePly: `scratch` is
// static and holds at least scratchCapacity entries.
inline Move bestMove(const Game& game, Move* scratch, int scratchCapacity, uint32_t& rngState,
                     Level level = Level::Medium) {
  if (level == Level::Hard) return bestTwoPly(game, scratch, scratchCapacity, rngState);
  if (level == Level::Easy && (nextRandom(rngState) % 100u) < 50u) {
    const int count = generateMoves(game, scratch, scratchCapacity);
    if (count > 0) return scratch[nextRandom(rngState) % static_cast<uint32_t>(count)];
  }
  return bestOnePly(game, scratch, scratchCapacity, rngState);
}

}  // namespace tak

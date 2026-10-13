#pragma once

// A Tak game in progress, as bytes. Freestanding: the Activity does the file I/O
// and this does the format, so the format can be tested without a card.
//
// Written by hand, field by field, rather than as a dump of the structs. A raw
// dump would carry the compiler's padding, would read back a `bool` from
// whatever byte happened to be there (undefined if it is not 0 or 1), and would
// change meaning silently whenever Game gained a field. The cost is one encoder
// and one decoder; the benefit is that decode() can say NO to a file that is
// short, long, from another version, or simply not a position this game could
// have reached -- and a corrupted save then costs the player the saved game
// rather than a board that draws garbage or a rules engine fed impossible
// state.
//
// What is checked is more than the shape. The pieces on the board plus the
// pieces in each reserve must add up to exactly the stones the board size
// starts with, per colour; whose turn it is must follow from the move number;
// and only squares of the board may hold anything. A position that fails any of
// those did not come from this app.
//
// The version byte is the only migration plan: change the layout and bump it,
// and an older save is discarded rather than misread.

#include <cstddef>
#include <cstdint>

#include "TakCore.h"
#include "TakFlow.h"

namespace tak {

constexpr uint8_t kSaveVersion = 1;

// What is worth keeping: the position, and the settings it was played under.
struct Saved {
  Game game;
  Opponent opponent = Opponent::Computer;
  Color player = Color::White;
  Level level = Level::Medium;
  // So a resumed game plays on from the same sequence of the computer's choices.
  uint32_t rng = 1;
};

// 1 version, 5 settings, 2 ply, 1 flags, 4 reserve, 4 rng, then a board's worth
// of squares at (1 height + 8 owners + 1 kind) each.
constexpr size_t kSavedBytes = 1 + 5 + 2 + 1 + 4 + 4 + kCells * 10;

namespace detail {
inline void put64(uint8_t* out, uint64_t value) {
  for (int i = 0; i < 8; ++i) out[i] = static_cast<uint8_t>(value >> (8 * i));
}
inline uint64_t get64(const uint8_t* in) {
  uint64_t value = 0;
  for (int i = 0; i < 8; ++i) value |= static_cast<uint64_t>(in[i]) << (8 * i);
  return value;
}
}  // namespace detail

// Writes `saved` into `out` (at least kSavedBytes) and returns the byte count.
inline size_t encodeSaved(const Saved& saved, uint8_t* out) {
  const Game& game = saved.game;
  size_t n = 0;
  out[n++] = kSaveVersion;
  out[n++] = static_cast<uint8_t>(saved.opponent);
  out[n++] = static_cast<uint8_t>(saved.player);
  out[n++] = static_cast<uint8_t>(saved.level);
  out[n++] = game.size;
  out[n++] = static_cast<uint8_t>(game.toMove);
  out[n++] = static_cast<uint8_t>(game.plyNumber & 0xFF);
  out[n++] = static_cast<uint8_t>(game.plyNumber >> 8);
  out[n++] = static_cast<uint8_t>((game.over ? 1 : 0) | (game.winnerByRoad ? 2 : 0) | (game.draw ? 4 : 0) |
                                  (game.winner == Color::Black ? 8 : 0));
  for (const Reserve& reserve : game.reserve) {
    out[n++] = reserve.flats;
    out[n++] = reserve.capstones;
  }
  for (int i = 0; i < 4; ++i) out[n++] = static_cast<uint8_t>(saved.rng >> (8 * i));
  for (const Cell& cell : game.board) {
    out[n++] = cell.height;
    detail::put64(out + n, cell.colors);
    n += 8;
    out[n++] = static_cast<uint8_t>(cell.topKind);
  }
  return n;
}

// Reads a save, or says no. On false, `out` is untouched.
inline bool decodeSaved(const uint8_t* in, const size_t length, Saved& out) {
  if (length != kSavedBytes || in[0] != kSaveVersion) return false;
  Saved saved;
  Game& game = saved.game;
  size_t n = 1;
  if (in[n] > 1) return false;
  saved.opponent = static_cast<Opponent>(in[n++]);
  if (in[n] > 1) return false;
  saved.player = static_cast<Color>(in[n++]);
  if (in[n] > 2) return false;
  saved.level = static_cast<Level>(in[n++]);
  const int size = in[n++];
  if (!validSize(size)) return false;
  game.size = static_cast<uint8_t>(size);
  if (in[n] > 1) return false;
  game.toMove = static_cast<Color>(in[n++]);
  game.plyNumber = static_cast<uint16_t>(in[n] | (in[n + 1] << 8));
  n += 2;
  const uint8_t flags = in[n++];
  if (flags > 15) return false;
  game.over = (flags & 1) != 0;
  game.winnerByRoad = (flags & 2) != 0;
  game.draw = (flags & 4) != 0;
  game.winner = (flags & 8) != 0 ? Color::Black : Color::White;
  for (Reserve& reserve : game.reserve) {
    reserve.flats = in[n++];
    reserve.capstones = in[n++];
    if (reserve.flats > flatsFor(size) || reserve.capstones > capstonesFor(size)) return false;
  }
  saved.rng = static_cast<uint32_t>(in[n]) | (static_cast<uint32_t>(in[n + 1]) << 8) |
              (static_cast<uint32_t>(in[n + 2]) << 16) | (static_cast<uint32_t>(in[n + 3]) << 24);
  n += 4;

  // A game that has ended is not one to resume.
  if (game.over || game.plyNumber > 1000) return false;
  // White moves on even plies, Black on odd ones, from the first.
  if (game.toMove != ((game.plyNumber % 2) == 0 ? Color::White : Color::Black)) return false;

  int stones[2] = {0, 0};
  int caps[2] = {0, 0};
  for (int i = 0; i < kCells; ++i) {
    Cell& cell = game.board[i];
    cell.height = in[n++];
    cell.colors = detail::get64(in + n);
    n += 8;
    if (in[n] > 2) return false;
    cell.topKind = static_cast<PieceKind>(in[n++]);
    if (!playable(game, i)) {
      if (cell.height != 0 || cell.colors != 0) return false;
      cell.topKind = PieceKind::Flat;
      continue;
    }
    if (cell.height > kMaxStackHeight) return false;
    if (cell.height == 0) {
      if (cell.colors != 0) return false;
      cell.topKind = PieceKind::Flat;
      continue;
    }
    // Only the levels that exist may carry an owner.
    if ((cell.colors >> cell.height) != 0) return false;
    for (int level = 0; level < cell.height; ++level) {
      const bool top = level == cell.height - 1;
      const int owner = static_cast<int>(colorAt(cell, level));
      if (top && cell.topKind == PieceKind::Cap) {
        ++caps[owner];
      } else {
        ++stones[owner];
      }
    }
  }
  // Every stone is on the board or in a reserve: nothing made, nothing lost.
  for (int color = 0; color < 2; ++color) {
    if (stones[color] + game.reserve[color].flats != flatsFor(size)) return false;
    if (caps[color] + game.reserve[color].capstones != capstonesFor(size)) return false;
  }
  out = saved;
  return true;
}

}  // namespace tak

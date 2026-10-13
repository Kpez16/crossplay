// The saved-game format, checked without a card.
//
// The property that matters is the round trip at every point of a real game,
// on both boards: whatever the Activity writes after a move must read back as
// exactly the position it wrote, because that is what "pick up where you left
// off" means. The rest is the other direction -- a file that is not a position
// this app could have saved must be refused rather than loaded.

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "TakBrain.h"
#include "TakCore.h"
#include "TakSave.h"

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

Move gMoves[kMaxMoves];

bool sameGame(const Game& a, const Game& b) {
  if (a.size != b.size || a.toMove != b.toMove || a.plyNumber != b.plyNumber || a.over != b.over ||
      a.winnerByRoad != b.winnerByRoad || a.draw != b.draw) {
    return false;
  }
  if (a.over && !a.draw && a.winner != b.winner) return false;
  for (int i = 0; i < 2; ++i) {
    if (a.reserve[i].flats != b.reserve[i].flats || a.reserve[i].capstones != b.reserve[i].capstones) return false;
  }
  for (int i = 0; i < kCells; ++i) {
    if (a.board[i].height != b.board[i].height) return false;
    if (a.board[i].height == 0) continue;
    if (a.board[i].colors != b.board[i].colors || a.board[i].topKind != b.board[i].topKind) return false;
  }
  return true;
}

Saved midGameSave(const int size) {
  Saved saved;
  reset(saved.game, size);
  uint32_t rng = 77;
  for (int i = 0; i < (size == kSmallSize ? 4 : 14) && !saved.game.over; ++i) {
    const Move move = bestMove(saved.game, gMoves, kMaxMoves, rng, Level::Easy);
    makeMove(saved.game, move);
  }
  saved.opponent = Opponent::Computer;
  saved.player = Color::Black;
  saved.level = Level::Hard;
  saved.rng = rng;
  return saved;
}

void testEveryPointOfARealGameRoundTrips() {
  const int sizes[2] = {kSize, kSmallSize};
  for (const int size : sizes) {
    uint32_t rng = 4242 + static_cast<uint32_t>(size);
    for (int g = 0; g < 30; ++g) {
      Saved saved;
      reset(saved.game, size);
      saved.opponent = (g % 2) == 0 ? Opponent::Computer : Opponent::Human;
      saved.player = (g % 3) == 0 ? Color::Black : Color::White;
      saved.level = static_cast<Level>(g % 3);
      for (int ply = 0; ply < 200 && !saved.game.over; ++ply) {
        const Move move = bestMove(saved.game, gMoves, kMaxMoves, rng, Level::Easy);
        CHECK(makeMove(saved.game, move));
        if (saved.game.over) break;
        saved.rng = rng;
        uint8_t bytes[kSavedBytes];
        CHECK(encodeSaved(saved, bytes) == kSavedBytes);
        Saved back;
        const bool ok = decodeSaved(bytes, kSavedBytes, back);
        CHECK(ok);
        if (!ok) return;
        CHECK(sameGame(saved.game, back.game));
        CHECK(back.opponent == saved.opponent && back.player == saved.player && back.level == saved.level);
        CHECK(back.rng == saved.rng);
      }
    }
  }
}

void testAResumedGameIsPlayableIdentically() {
  // Same position, same generator: the same reply, with or without the card in between.
  Saved saved = midGameSave(kSize);
  uint8_t bytes[kSavedBytes];
  encodeSaved(saved, bytes);
  Saved back;
  CHECK(decodeSaved(bytes, kSavedBytes, back));
  uint32_t a = saved.rng;
  uint32_t b = back.rng;
  const Move first = bestMove(saved.game, gMoves, kMaxMoves, a, Level::Hard);
  Game x = saved.game;
  CHECK(makeMove(x, first));
  const Move second = bestMove(back.game, gMoves, kMaxMoves, b, Level::Hard);
  Game y = back.game;
  CHECK(makeMove(y, second));
  CHECK(sameGame(x, y));
}

void testABadFileIsRefused() {
  Saved saved = midGameSave(kSize);
  uint8_t good[kSavedBytes];
  encodeSaved(saved, good);
  Saved out;
  CHECK(decodeSaved(good, kSavedBytes, out));

  CHECK(!decodeSaved(good, kSavedBytes - 1, out));  // short
  CHECK(!decodeSaved(good, 0, out));
  uint8_t longer[kSavedBytes + 1];
  std::memcpy(longer, good, kSavedBytes);
  longer[kSavedBytes] = 0;
  CHECK(!decodeSaved(longer, kSavedBytes + 1, out));  // long

  uint8_t bad[kSavedBytes];
  const auto mutated = [&](const size_t at, const uint8_t value) {
    std::memcpy(bad, good, kSavedBytes);
    bad[at] = value;
    return !decodeSaved(bad, kSavedBytes, out);
  };
  CHECK(mutated(0, kSaveVersion + 1));  // another version
  CHECK(mutated(1, 2));                 // not an opponent
  CHECK(mutated(2, 2));                 // not a colour
  CHECK(mutated(3, 3));                 // not a level
  CHECK(mutated(4, 4));                 // not a board size
  CHECK(mutated(5, good[5] ^ 1));       // the wrong side to move for this ply
  CHECK(mutated(8, 1));                 // a finished game is not resumed
  CHECK(mutated(9, 99));                // more flats than the board starts with
  CHECK(mutated(10, 99));
  // A stone conjured from nothing: one more stone on a square, same reserve.
  const size_t firstCell = 1 + 5 + 2 + 1 + 4 + 4;
  size_t emptyCell = 0;
  for (int i = 0; i < kCells; ++i) {
    if (saved.game.board[i].height == 0) {
      emptyCell = firstCell + static_cast<size_t>(i) * 10;
      break;
    }
  }
  CHECK(emptyCell != 0);
  CHECK(mutated(emptyCell, 1));
  CHECK(mutated(emptyCell + 9, 7));  // not a kind of stone
}

void testASmallBoardSaveCannotHoldStonesOffTheBoard() {
  Saved saved = midGameSave(kSmallSize);
  uint8_t bytes[kSavedBytes];
  encodeSaved(saved, bytes);
  Saved out;
  CHECK(decodeSaved(bytes, kSavedBytes, out));
  CHECK(out.game.size == 3);
  const size_t firstCell = 1 + 5 + 2 + 1 + 4 + 4;
  // Column 4 of row 0 is not a square of a 3x3 board.
  bytes[firstCell + static_cast<size_t>(at(4, 0)) * 10] = 1;
  CHECK(!decodeSaved(bytes, kSavedBytes, out));
}

}  // namespace

int main() {
  testEveryPointOfARealGameRoundTrips();
  testAResumedGameIsPlayableIdentically();
  testABadFileIsRefused();
  testASmallBoardSaveCannotHoldStonesOffTheBoard();
  std::printf("%d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
